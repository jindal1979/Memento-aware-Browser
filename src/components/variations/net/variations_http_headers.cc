// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/variations/net/variations_http_headers.h"

#include <stddef.h>

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/macros.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "components/google/core/common/google_util.h"
#include "components/variations/net/omnibox_http_headers.h"
#include "components/variations/variations_http_header_provider.h"
#include "net/base/isolation_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace variations {

// The name string for the header for variations information.
// Note that prior to M33 this header was named X-Chrome-Variations.
const char kClientDataHeader[] = "X-Client-Data";

namespace {

// The result of checking whether a request to a URL should have variations
// headers appended to it.
//
// This enum is used to record UMA histogram values, and should not be
// reordered.
enum class URLValidationResult {
  kNotValidInvalidUrl = 0,
  // kNotValidNotHttps = 1,  // Deprecated.
  kNotValidNotGoogleDomain = 2,
  kShouldAppend = 3,
  kNotValidNeitherHttpHttps = 4,
  kNotValidIsGoogleNotHttps = 5,
  kMaxValue = kNotValidIsGoogleNotHttps,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum RequestContextCategory {
  // First-party contexts.
  kBrowserInitiated = 0,
  kInternalChromePageInitiated = 1,
  kGooglePageInitiated = 2,
  kGoogleSubFrameOnGooglePageInitiated = 3,
  // Third-party contexts.
  kNonGooglePageInitiatedFromRequestInitiator = 4,
  kNoTrustedParams = 5,
  kNoIsolationInfo = 6,
  kGoogleSubFrameOnNonGooglePageInitiated = 7,
  kNonGooglePageInitiatedFromFrameOrigin = 8,
  kMaxValue = kNonGooglePageInitiatedFromFrameOrigin,
};

void LogRequestContextHistogram(RequestContextCategory result) {
  base::UmaHistogramEnumeration("Variations.Headers.RequestContextCategory",
                                result);
}

// Returns a URLValidationResult for |url|. A valid URL for headers has the
// following qualities: (i) it is well-formed, (ii) its scheme is HTTPS, and
// (iii) it has a Google-associated domain.
URLValidationResult GetUrlValidationResult(const GURL& url) {
  if (!url.is_valid())
    return URLValidationResult::kNotValidInvalidUrl;

  if (!url.SchemeIsHTTPOrHTTPS())
    return URLValidationResult::kNotValidNeitherHttpHttps;

  if (!google_util::IsGoogleAssociatedDomainUrl(url))
    return URLValidationResult::kNotValidNotGoogleDomain;

  // HTTPS is checked here, rather than before the IsGoogleAssociatedDomainUrl()
  // check, to know how many Google domains are rejected by the change to append
  // headers to only HTTPS requests.
  if (!url.SchemeIs(url::kHttpsScheme))
    return URLValidationResult::kNotValidIsGoogleNotHttps;

  return URLValidationResult::kShouldAppend;
}

// Returns true if the request to |url| should include a variations header.
// Also, logs the result of validating |url| in a histogram.
bool ShouldAppendVariationsHeader(const GURL& url) {
  URLValidationResult result = GetUrlValidationResult(url);
  UMA_HISTOGRAM_ENUMERATION("Variations.Headers.URLValidationResult", result);
  return result == URLValidationResult::kShouldAppend;
}

// Returns true if the request is sent from a Google-associated property, i.e.
// from a first-party context. This determination is made using the request
// context derived from |resource_request|.
bool IsFirstPartyContext(const network::ResourceRequest& resource_request) {
  if (!resource_request.request_initiator) {
    // The absence of |request_initiator| means that the request was initiated
    // by the browser, e.g. a request from the browser to Autofill upon form
    // detection.
    LogRequestContextHistogram(RequestContextCategory::kBrowserInitiated);
    return true;
  }

  const GURL request_initiator_url =
      resource_request.request_initiator->GetURL();
  if (request_initiator_url.SchemeIs("chrome-search") ||
      request_initiator_url.SchemeIs("chrome")) {
    // A scheme matching the above patterns means that the request was
    // initiated by an internal page, e.g. a request from
    // chrome-search://local-ntp/ for App Launcher resources.
    LogRequestContextHistogram(kInternalChromePageInitiated);
    return true;
  }
  if (GetUrlValidationResult(request_initiator_url) !=
      URLValidationResult::kShouldAppend) {
    // The request was initiated by a non-Google-associated page, e.g. a request
    // from https://www.bbc.com/.
    LogRequestContextHistogram(kNonGooglePageInitiatedFromRequestInitiator);
    return false;
  }
  if (resource_request.is_main_frame) {
    // The request is from a Google-associated page--not a sub-frame--e.g. a
    // request from https://calendar.google.com/.
    LogRequestContextHistogram(kGooglePageInitiated);
    return true;
  }
  if (!resource_request.trusted_params) {
    LogRequestContextHistogram(kNoTrustedParams);
    // Without TrustedParams, we cannot be certain that the request is from a
    // first-party context.
    return false;
  }

  const net::IsolationInfo* isolation_info =
      &resource_request.trusted_params->isolation_info;
  if (isolation_info->IsEmpty()) {
    LogRequestContextHistogram(kNoIsolationInfo);
    // Without IsolationInfo, we cannot be certain that the request is from a
    // first-party context.
    return false;
  }
  if (GetUrlValidationResult(isolation_info->top_frame_origin()->GetURL()) !=
      URLValidationResult::kShouldAppend) {
    // The request is from a Google-associated sub-frame on a
    // non-Google-associated page, e.g. a request to DoubleClick from an ad's
    // sub-frame on https://www.lexico.com/.
    LogRequestContextHistogram(kGoogleSubFrameOnNonGooglePageInitiated);
    return false;
  }
  if (GetUrlValidationResult(isolation_info->frame_origin()->GetURL()) !=
      URLValidationResult::kShouldAppend) {
    // The request was initiated by a non-Google-associated page, e.g. a request
    // from https://www.bbc.com/.
    //
    // TODO(crbug/1094303): This case should be covered by checking the request
    // initiator's URL. Maybe deprecate kNonGooglePageInitiatedFromFrameOrigin
    // if this bucket is never used.
    LogRequestContextHistogram(kNonGooglePageInitiatedFromFrameOrigin);
    return false;
  }
  // The request is from a Google-associated sub-frame on a Google-associated
  // page, e.g. a request from a Docs sub-frame on https://drive.google.com/.
  LogRequestContextHistogram(kGoogleSubFrameOnGooglePageInitiated);
  return true;
}

class VariationsHeaderHelper {
 public:
  // Note: It's OK to pass SignedIn::kNo if it's unknown, as it does not affect
  // transmission of experiments coming from the variations server.
  VariationsHeaderHelper(network::ResourceRequest* request,
                         SignedIn signed_in = SignedIn::kNo)
      : VariationsHeaderHelper(request, CreateVariationsHeader(signed_in)) {}
  VariationsHeaderHelper(network::ResourceRequest* resource_request,
                         std::string variations_header)
      : resource_request_(resource_request) {
    DCHECK(resource_request_);
    variations_header_ = std::move(variations_header);
  }

  bool AppendHeaderIfNeeded(const GURL& url, InIncognito incognito) {
    AppendOmniboxOnDeviceSuggestionsHeaderIfNeeded(url, resource_request_);

    // Note the criteria for attaching client experiment headers:
    // 1. We only transmit to Google owned domains which can evaluate
    // experiments.
    //    1a. These include hosts which have a standard postfix such as:
    //         *.doubleclick.net or *.googlesyndication.com or
    //         exactly www.googleadservices.com or
    //         international TLD domains *.google.<TLD> or *.youtube.<TLD>.
    // 2. Only transmit for non-Incognito profiles.
    // 3. For the X-Client-Data header, only include non-empty variation IDs.
    if ((incognito == InIncognito::kYes) || !ShouldAppendVariationsHeader(url))
      return false;

    // TODO(crbug/1094303): Use the result to determine which IDs to include.
    IsFirstPartyContext(*resource_request_);

    if (variations_header_.empty())
      return false;

    // Set the variations header to cors_exempt_headers rather than headers
    // to be exempted from CORS checks.
    resource_request_->cors_exempt_headers.SetHeaderIfMissing(
        kClientDataHeader, variations_header_);
    return true;
  }

 private:
  static std::string CreateVariationsHeader(SignedIn signed_in) {
    return VariationsHttpHeaderProvider::GetInstance()->GetClientDataHeader(
        signed_in == SignedIn::kYes);
  }

  network::ResourceRequest* resource_request_;
  std::string variations_header_;

  DISALLOW_COPY_AND_ASSIGN(VariationsHeaderHelper);
};

}  // namespace

bool AppendVariationsHeader(const GURL& url,
                            InIncognito incognito,
                            SignedIn signed_in,
                            network::ResourceRequest* request) {
  return VariationsHeaderHelper(request, signed_in)
      .AppendHeaderIfNeeded(url, incognito);
}

bool AppendVariationsHeaderWithCustomValue(const GURL& url,
                                           InIncognito incognito,
                                           const std::string& variations_header,
                                           network::ResourceRequest* request) {
  return VariationsHeaderHelper(request, variations_header)
      .AppendHeaderIfNeeded(url, incognito);
}

bool AppendVariationsHeaderUnknownSignedIn(const GURL& url,
                                           InIncognito incognito,
                                           network::ResourceRequest* request) {
  return VariationsHeaderHelper(request).AppendHeaderIfNeeded(url, incognito);
}

void RemoveVariationsHeaderIfNeeded(
    const net::RedirectInfo& redirect_info,
    const network::mojom::URLResponseHead& response_head,
    std::vector<std::string>* to_be_removed_headers) {
  if (!ShouldAppendVariationsHeader(redirect_info.new_url))
    to_be_removed_headers->push_back(kClientDataHeader);
}

std::unique_ptr<network::SimpleURLLoader>
CreateSimpleURLLoaderWithVariationsHeader(
    std::unique_ptr<network::ResourceRequest> request,
    InIncognito incognito,
    SignedIn signed_in,
    const net::NetworkTrafficAnnotationTag& annotation_tag) {
  bool variation_headers_added =
      AppendVariationsHeader(request->url, incognito, signed_in, request.get());
  std::unique_ptr<network::SimpleURLLoader> simple_url_loader =
      network::SimpleURLLoader::Create(std::move(request), annotation_tag);
  if (variation_headers_added) {
    simple_url_loader->SetOnRedirectCallback(
        base::BindRepeating(&RemoveVariationsHeaderIfNeeded));
  }
  return simple_url_loader;
}

std::unique_ptr<network::SimpleURLLoader>
CreateSimpleURLLoaderWithVariationsHeaderUnknownSignedIn(
    std::unique_ptr<network::ResourceRequest> request,
    InIncognito incognito,
    const net::NetworkTrafficAnnotationTag& annotation_tag) {
  return CreateSimpleURLLoaderWithVariationsHeader(
      std::move(request), incognito, SignedIn::kNo, annotation_tag);
}

bool IsVariationsHeader(const std::string& header_name) {
  return header_name == kClientDataHeader ||
         header_name == kOmniboxOnDeviceSuggestionsHeader;
}

bool HasVariationsHeader(const network::ResourceRequest& request) {
  // Note: kOmniboxOnDeviceSuggestionsHeader is not listed because this function
  // is only used for testing.
  return request.cors_exempt_headers.HasHeader(kClientDataHeader);
}

bool ShouldAppendVariationsHeaderForTesting(const GURL& url) {
  return ShouldAppendVariationsHeader(url);
}

void UpdateCorsExemptHeaderForVariations(
    network::mojom::NetworkContextParams* params) {
  params->cors_exempt_header_list.push_back(kClientDataHeader);

  if (base::FeatureList::IsEnabled(kReportOmniboxOnDeviceSuggestionsHeader)) {
    params->cors_exempt_header_list.push_back(
        kOmniboxOnDeviceSuggestionsHeader);
  }
}

}  // namespace variations
