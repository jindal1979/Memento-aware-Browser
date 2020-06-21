// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/memory/ref_counted.h"
#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/api/dns/dns_api.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/browser/notification_types.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/shell/test/shell_apitest.h"
#include "extensions/test/result_catcher.h"
#include "net/base/features.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/network_isolation_key.h"
#include "net/dns/mock_host_resolver.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/test/test_dns_util.h"
#include "url/origin.h"

namespace extensions {
namespace {
using extensions::api_test_utils::RunFunctionAndReturnSingleResult;

constexpr char kHostname[] = "www.sowbug.test";
constexpr char kAddress[] = "9.8.7.6";
}  // namespace

class DnsApiTest : public ShellApiTest {
 public:
  DnsApiTest() {
    // Enable kSplitHostCacheByNetworkIsolationKey so the test can verify that
    // the correct NetworkIsolationKey was used for the DNS lookup.
    scoped_feature_list_.InitAndEnableFeature(
        net::features::kSplitHostCacheByNetworkIsolationKey);
  }

 private:
  void SetUpOnMainThread() override {
    ShellApiTest::SetUpOnMainThread();
    host_resolver()->AddRule(kHostname, kAddress);
    host_resolver()->AddSimulatedFailure("this.hostname.is.bogus.test");
  }

  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(DnsApiTest, DnsResolveIPLiteral) {
  scoped_refptr<DnsResolveFunction> resolve_function(new DnsResolveFunction());
  scoped_refptr<const Extension> empty_extension =
      ExtensionBuilder("Test").Build();

  resolve_function->set_extension(empty_extension.get());
  resolve_function->set_has_callback(true);

  std::unique_ptr<base::Value> result(RunFunctionAndReturnSingleResult(
      resolve_function.get(), "[\"127.0.0.1\"]", browser_context()));
  base::DictionaryValue* dict = NULL;
  ASSERT_TRUE(result->GetAsDictionary(&dict));

  int result_code = 0;
  EXPECT_TRUE(dict->GetInteger("resultCode", &result_code));
  EXPECT_EQ(net::OK, result_code);

  std::string address;
  EXPECT_TRUE(dict->GetString("address", &address));
  EXPECT_EQ("127.0.0.1", address);
}

IN_PROC_BROWSER_TEST_F(DnsApiTest, DnsResolveHostname) {
  ResultCatcher catcher;
  const Extension* extension = LoadExtension("extension");
  ASSERT_TRUE(extension);
  ASSERT_TRUE(catcher.GetNextResult());

  auto resolve_function = base::MakeRefCounted<DnsResolveFunction>();
  resolve_function->set_extension(extension);
  resolve_function->set_has_callback(true);

  std::string function_arguments = base::StringPrintf(R"(["%s"])", kHostname);
  std::unique_ptr<base::Value> result(RunFunctionAndReturnSingleResult(
      resolve_function.get(), function_arguments, browser_context()));
  base::DictionaryValue* dict = NULL;
  ASSERT_TRUE(result->GetAsDictionary(&dict));

  int result_code = 0;
  EXPECT_TRUE(dict->GetInteger("resultCode", &result_code));
  EXPECT_EQ(net::OK, result_code);

  std::string address;
  EXPECT_TRUE(dict->GetString("address", &address));
  EXPECT_EQ(kAddress, address);

  // Make sure the extension's NetworkIsolationKey was used. Do a cache only DNS
  // lookup using the expected NIK, and make sure the IP address is retrieved.
  network::mojom::NetworkContext* network_context =
      content::BrowserContext::GetDefaultStoragePartition(browser_context())
          ->GetNetworkContext();
  net::HostPortPair host_port_pair(kHostname, 0);
  network::mojom::ResolveHostParametersPtr params =
      network::mojom::ResolveHostParameters::New();
  // Cache only lookup.
  params->source = net::HostResolverSource::LOCAL_ONLY;
  url::Origin origin = url::Origin::Create(extension->url());
  net::NetworkIsolationKey network_isolation_key(origin, origin);
  network::DnsLookupResult result1 =
      network::BlockingDnsLookup(network_context, host_port_pair,
                                 std::move(params), network_isolation_key);
  EXPECT_EQ(net::OK, result1.error);
  ASSERT_TRUE(result1.resolved_addresses.has_value());
  ASSERT_EQ(1u, result1.resolved_addresses->size());
  EXPECT_EQ(kAddress,
            result1.resolved_addresses.value()[0].ToStringWithoutPort());

  // Check that the entry isn't present in the cache with the empty
  // NetworkIsolationKey.
  params = network::mojom::ResolveHostParameters::New();
  // Cache only lookup.
  params->source = net::HostResolverSource::LOCAL_ONLY;
  network::DnsLookupResult result2 =
      network::BlockingDnsLookup(network_context, host_port_pair,
                                 std::move(params), net::NetworkIsolationKey());
  EXPECT_EQ(net::ERR_NAME_NOT_RESOLVED, result2.error);
}

IN_PROC_BROWSER_TEST_F(DnsApiTest, DnsExtension) {
  ASSERT_TRUE(RunAppTest("api_test/dns/api")) << message_;
}

}  // namespace extensions
