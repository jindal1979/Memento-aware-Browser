// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/flags/android/chrome_feature_list.h"

#include <stddef.h>

#include <string>

#include "base/android/jni_string.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/stl_util.h"
#include "chrome/browser/flags/jni_headers/ChromeFeatureList_jni.h"
#include "chrome/browser/share/features.h"
#include "chrome/browser/sharing/shared_clipboard/feature_flags.h"
#include "chrome/common/chrome_features.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill_assistant/browser/features.h"
#include "components/browser_sync/browser_sync_switches.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_features.h"
#include "components/download/public/common/download_features.h"
#include "components/feature_engagement/public/feature_list.h"
#include "components/feed/feed_feature_list.h"
#include "components/invalidation/impl/invalidation_switches.h"
#include "components/language/core/common/language_experiments.h"
#include "components/ntp_snippets/features.h"
#include "components/ntp_tiles/features.h"
#include "components/offline_pages/core/offline_page_feature.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/paint_preview/features/features.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/permissions/features.h"
#include "components/previews/core/previews_features.h"
#include "components/query_tiles/switches.h"
#include "components/safe_browsing/core/features.h"
#include "components/security_state/core/features.h"
#include "components/signin/public/base/account_consistency_method.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/sync/driver/sync_driver_switches.h"
#include "content/public/common/content_features.h"
#include "device/fido/features.h"
#include "media/base/media_switches.h"
#include "net/base/features.h"
#include "services/device/public/cpp/device_features.h"
#include "ui/base/ui_base_features.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace chrome {
namespace android {

namespace {

// Array of features exposed through the Java ChromeFeatureList API. Entries in
// this array may either refer to features defined in the header of this file or
// in other locations in the code base (e.g. chrome/, components/, etc).
const base::Feature* kFeaturesExposedToJava[] = {
    &autofill::features::kAutofillCreditCardAuthentication,
    &autofill::features::kAutofillKeyboardAccessory,
    &autofill::features::kAutofillManualFallbackAndroid,
    &autofill::features::kAutofillRefreshStyleAndroid,
    &autofill::features::kAutofillEnableCardNicknameManagement,
    &autofill::features::kAutofillEnableCompanyName,
    &autofill::features::kAutofillEnableGoogleIssuedCard,
    &autofill::features::kAutofillEnableSurfacingServerCardNickname,
    &autofill_assistant::features::kAutofillAssistant,
    &autofill_assistant::features::kAutofillAssistantChromeEntry,
    &autofill_assistant::features::kAutofillAssistantDirectActions,
    &autofill::features::kAutofillTouchToFill,
    &device::kWebAuthPhoneSupport,
    &download::features::kDownloadAutoResumptionNative,
    &download::features::kDownloadLater,
    &download::features::kUseDownloadOfflineContentProvider,
    &features::kClearOldBrowsingData,
    &features::kDownloadsLocationChange,
    &features::kGenericSensorExtraClasses,
    &features::kInstallableAmbientBadgeInfoBar,
    &features::kNetworkServiceInProcess,
    &features::kOverscrollHistoryNavigation,
    &features::kPredictivePrefetchingAllowedOnAllConnectionTypes,
    &features::kPrioritizeBootstrapTasks,
    &features::kQuietNotificationPrompts,
    &features::kSafetyCheckAndroid,
    &features::kShowTrustedPublisherURL,
    &features::kWebAuth,
    &features::kWebNfc,
    &feature_engagement::kIPHChromeDuetHomeButtonFeature,
    &feature_engagement::kIPHChromeDuetSearchFeature,
    &feature_engagement::kIPHChromeDuetTabSwitcherFeature,
    &feature_engagement::kIPHHomepagePromoCardFeature,
    &feed::kInterestFeedContentSuggestions,
    &feed::kInterestFeedFeedback,
    &feed::kInterestFeedV2,
    &feed::kReportFeedUserActions,
    &kAdjustWebApkInstallationSpace,
    &kAllowNewIncognitoTabIntents,
    &kAllowRemoteContextForNotifications,
    &kAndroidBlockIntentNonSafelistedHeaders,
    &kAndroidDefaultBrowserPromo,
    &kAndroidMultipleDisplay,
    &kAndroidNightModeTabReparenting,
    &kAndroidPartnerCustomizationPhenotype,
    &kAndroidPayIntegrationV2,
    &kAndroidSearchEngineChoiceNotification,
    &kCastDeviceFilter,
    &kCloseTabSuggestions,
    &kCCTBackgroundTab,
    &kCCTClientDataHeader,
    &kCCTExternalLinkHandling,
    &kCCTIncognito,
    &kCCTPostMessageAPI,
    &kCCTRedirectPreconnect,
    &kCCTReportParallelRequestStatus,
    &kCCTResourcePrefetch,
    &kCCTTargetTranslateLanguage,
    &kChromeDuetFeature,
    &kChromeDuetAdaptive,
    &kDarkenWebsitesCheckboxInThemesSetting,
    &kDontAutoHideBrowserControls,
    &kChromeDuetLabeled,
    &kChromeShareQRCode,
    &kChromeShareScreenshot,
    &kChromeSharingHub,
    &kChromeSharingHubV15,
    &kChromeSmartSelection,
    &kCommandLineOnNonRooted,
    &kConditionalTabStripAndroid,
    &kContactsPickerSelectAll,
    &kContentIndexingDownloadHome,
    &kContentIndexingNTP,
    &kContentSuggestionsScrollToLoad,
    &kContextMenuCopyImage,
    &kContextMenuPerformanceInfo,
    &kContextMenuSearchWithGoogleLens,
    &kContextualSearchDebug,
    &kContextualSearchDefinitions,
    &kContextualSearchLongpressResolve,
    &kContextualSearchMlTapSuppression,
    &kContextualSearchSecondTap,
    &kContextualSearchTapDisableOverride,
    &kContextualSearchTranslations,
    &kDirectActions,
    &kDownloadFileProvider,
    &kDownloadNotificationBadge,
    &kDownloadProgressInfoBar,
    &kDownloadRename,
    &kDrawVerticallyEdgeToEdge,
    &kDuetTabStripIntegrationAndroid,
    &kEphemeralTabUsingBottomSheet,
    &kExploreSites,
    &kFocusOmniboxInIncognitoTabIntents,
    &kHandleMediaIntents,
    &kHomepageLocation,
    &kHomepagePromoCard,
    &kHomepagePromoSyntheticPromoSeenEnabled,
    &kHomepagePromoSyntheticPromoSeenTracking,
    &kHomepageSettingsUIConversion,
    &kHorizontalTabSwitcherAndroid,
    &kImmersiveUiMode,
    &kInlineUpdateFlow,
    &kInstantStart,
    &kKitKatSupported,
    &kNewPhotoPicker,
    &kNotificationSuspender,
    &kOfflineIndicatorV2,
    &kOmniboxSpareRenderer,
    &kOverlayNewLayout,
    &kPageInfoPerformanceHints,
    &kPayWithGoogleV1,
    &kPhotoPickerVideoSupport,
    &kPhotoPickerZoom,
    &kProbabilisticCryptidRenderer,
    &kReachedCodeProfiler,
    &kReaderModeInCCT,
    &kRelatedSearches,
    &kRevampedContextMenu,
    &kSearchEnginePromoExistingDevice,
    &kSearchEnginePromoNewDevice,
    &kServiceManagerForBackgroundPrefetch,
    &kServiceManagerForDownload,
    &kShareButtonInTopToolbar,
    &kSharedClipboardUI,
    &kSharingQrCodeAndroid,
    &kShoppingAssist,
    &kSpannableInlineAutocomplete,
    &kSpecialLocaleWrapper,
    &kSpecialUserDecision,
    &kSwapPixelFormatToFixConvertFromTranslucent,
    &kTabEngagementReportingAndroid,
    &kTabGroupsAndroid,
    &kTabGroupsContinuationAndroid,
    &kTabGroupsUiImprovementsAndroid,
    &kTabGridLayoutAndroid,
    &kTabReparenting,
    &kTabSwitcherOnReturn,
    &kTabToGTSAnimation,
    &kTestDefaultDisabled,
    &kTestDefaultEnabled,
    &kTrustedWebActivityNewDisclosure,
    &kTrustedWebActivityLocationDelegation,
    &kTrustedWebActivityPostMessage,
    &kStartSurfaceAndroid,
    &kUmaBackgroundSessions,
    &kUpdateNotificationSchedulingIntegration,
    &kUpdateNotificationScheduleServiceImmediateShowOption,
    &kUsageStatsFeature,
    &kVideoPersistence,
    &kVrBrowsingFeedback,
    &kWebApkAdaptiveIcon,
    &kPrefetchNotificationSchedulingIntegration,
    &features::kDnsOverHttps,
    &net::features::kSameSiteByDefaultCookies,
    &net::features::kCookiesWithoutSameSiteMustBeSecure,
    &paint_preview::kPaintPreviewCaptureExperiment,
    &paint_preview::kPaintPreviewDemo,
    &paint_preview::kPaintPreviewShowOnStartup,
    &permissions::features::kPermissionDelegation,
    &language::kExplicitLanguageAsk,
    &ntp_snippets::kArticleSuggestionsFeature,
    &offline_pages::kOfflineIndicatorFeature,
    &offline_pages::kOfflineIndicatorAlwaysHttpProbeFeature,
    &offline_pages::kOfflinePagesCTFeature,    // See crbug.com/620421.
    &offline_pages::kOfflinePagesCTV2Feature,  // See crbug.com/734753.
    &offline_pages::kOfflinePagesDescriptiveFailStatusFeature,
    &offline_pages::kOfflinePagesDescriptivePendingStatusFeature,
    &offline_pages::kOfflinePagesLivePageSharingFeature,
    &offline_pages::kPrefetchingOfflinePagesFeature,
    &omnibox::kAdaptiveSuggestionsCount,
    &omnibox::kCompactSuggestions,
    &omnibox::kDeferredKeyboardPopup,
    &omnibox::kHideSteadyStateUrlScheme,
    &omnibox::kHideSteadyStateUrlTrivialSubdomains,
    &omnibox::kOmniboxAssistantVoiceSearch,
    &omnibox::kOmniboxSearchEngineLogo,
    &omnibox::kOmniboxSearchReadyIncognito,
    &omnibox::kOmniboxSuggestionsRecyclerView,
    &omnibox::kOmniboxSuggestionsWrapAround,
    &password_manager::features::kGooglePasswordManager,
    &password_manager::features::kPasswordCheck,
    &password_manager::features::kPasswordEditingAndroid,
    &password_manager::features::kPasswordManagerOnboardingAndroid,
    &password_manager::features::kRecoverFromNeverSaveAndroid,
    &query_tiles::features::kQueryTiles,
    &query_tiles::features::kQueryTilesInOmnibox,
    &query_tiles::features::kQueryTilesEnableQueryEditing,
    &security_state::features::kMarkHttpAsFeature,
    &signin::kMobileIdentityConsistency,
    &switches::kSyncErrorInfoBarAndroid,
    &switches::kSyncUseSessionsUnregisterDelay,
    &subresource_filter::kSafeBrowsingSubresourceFilter,
};

const base::Feature* FindFeatureExposedToJava(const std::string& feature_name) {
  for (const auto* feature : kFeaturesExposedToJava) {
    if (feature->name == feature_name)
      return feature;
  }
  NOTREACHED() << "Queried feature cannot be found in ChromeFeatureList: "
               << feature_name;
  return nullptr;
}

}  // namespace

// Alphabetical:
const base::Feature kAdjustWebApkInstallationSpace = {
    "AdjustWebApkInstallationSpace", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kAndroidBlockIntentNonSafelistedHeaders{
    "AndroidBlockIntentNonSafelistedHeaders", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAndroidDefaultBrowserPromo{
    "AndroidDefaultBrowserPromo", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kAndroidMultipleDisplay{"AndroidMultipleDisplay",
                                            base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAndroidNightModeTabReparenting{
    "AndroidNightModeTabReparenting", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAllowNewIncognitoTabIntents{
    "AllowNewIncognitoTabIntents", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kFocusOmniboxInIncognitoTabIntents{
    "FocusOmniboxInIncognitoTabIntents", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAllowRemoteContextForNotifications{
    "AllowRemoteContextForNotifications", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAndroidPartnerCustomizationPhenotype{
    "AndroidPartnerCustomizationPhenotype", base::FEATURE_DISABLED_BY_DEFAULT};

// TODO(rouslan): Remove this. (Currently used in
// GooglePayPaymentAppFactory.java)
const base::Feature kAndroidPayIntegrationV2{"AndroidPayIntegrationV2",
                                             base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kAndroidSearchEngineChoiceNotification{
    "AndroidSearchEngineChoiceNotification", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kBackgroundTaskComponentUpdate{
    "BackgroundTaskComponentUpdate", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kConditionalTabStripAndroid{
    "ConditionalTabStripAndroid", base::FEATURE_DISABLED_BY_DEFAULT};

// Used in downstream code.
const base::Feature kCastDeviceFilter{"CastDeviceFilter",
                                      base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCloseTabSuggestions{"CloseTabSuggestions",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCCTBackgroundTab{"CCTBackgroundTab",
                                      base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCCTClientDataHeader{"CCTClientDataHeader",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCCTExternalLinkHandling{"CCTExternalLinkHandling",
                                             base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCCTIncognito{"CCTIncognito",
                                  base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCCTPostMessageAPI{"CCTPostMessageAPI",
                                       base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCCTRedirectPreconnect{"CCTRedirectPreconnect",
                                           base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCCTReportParallelRequestStatus{
    "CCTReportParallelRequestStatus", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCCTResourcePrefetch{"CCTResourcePrefetch",
                                         base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCCTTargetTranslateLanguage{
    "CCTTargetTranslateLanguage", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kChromeDuetFeature{"ChromeDuet",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeDuetAdaptive{"ChromeDuetAdaptive",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDontAutoHideBrowserControls{
    "DontAutoHideBrowserControls", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeDuetLabeled{"ChromeDuetLabeled",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeShareQRCode{"ChromeShareQRCode",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeShareScreenshot{"ChromeShareScreenshot",
                                           base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeSharingHub{"ChromeSharingHub",
                                      base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeSharingHubV15{"ChromeSharingHubV15",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kChromeSmartSelection{"ChromeSmartSelection",
                                          base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCommandLineOnNonRooted{"CommandLineOnNonRooted",
                                            base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContactsPickerSelectAll{"ContactsPickerSelectAll",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContentIndexingDownloadHome{
    "ContentIndexingDownloadHome", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kContentIndexingNTP{"ContentIndexingNTP",
                                        base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContentSuggestionsScrollToLoad{
    "ContentSuggestionsScrollToLoad", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextMenuCopyImage{"ContextMenuCopyImage",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextMenuPerformanceInfo{
    "ContextMenuPerformanceInfo", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextMenuSearchWithGoogleLens{
    "ContextMenuSearchWithGoogleLens", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextualSearchDebug{"ContextualSearchDebug",
                                           base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextualSearchDefinitions{
    "ContextualSearchDefinitions", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kContextualSearchLongpressResolve{
    "ContextualSearchLongpressResolve", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextualSearchMlTapSuppression{
    "ContextualSearchMlTapSuppression", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextualSearchSecondTap{
    "ContextualSearchSecondTap", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextualSearchTapDisableOverride{
    "ContextualSearchTapDisableOverride", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kContextualSearchTranslations{
    "ContextualSearchTranslations", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kDarkenWebsitesCheckboxInThemesSetting{
    "DarkenWebsitesCheckboxInThemesSetting", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kDirectActions{"DirectActions",
                                   base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDrawVerticallyEdgeToEdge{
    "DrawVerticallyEdgeToEdge", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kDownloadAutoResumptionThrottling{
    "DownloadAutoResumptionThrottling", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDownloadProgressInfoBar{"DownloadProgressInfoBar",
                                             base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDownloadFileProvider{"DownloadFileProvider",
                                          base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDownloadNotificationBadge{
    "DownloadNotificationBadge", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDownloadRename{"DownloadRename",
                                    base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDuetTabStripIntegrationAndroid{
    "DuetTabStripIntegrationAndroid", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kEphemeralTabUsingBottomSheet{
    "EphemeralTabUsingBottomSheet", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kExploreSites{"ExploreSites",
                                  base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kHandleMediaIntents{"HandleMediaIntents",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

// Enable the HomePage Location feature that allows enterprise policy set and
// force the home page url for managed devices.
const base::Feature kHomepageLocation{"HomepageLocationPolicy",
                                      base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kHomepagePromoCard{"HomepagePromoCard",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

// Homepage Promo experiment group for synthetic field trial.
const base::Feature kHomepagePromoSyntheticPromoSeenEnabled{
    "HomepagePromoSyntheticPromoSeenEnabled",
    base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kHomepagePromoSyntheticPromoSeenTracking{
    "HomepagePromoSyntheticPromoSeenTracking",
    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kHomepageSettingsUIConversion{
    "HomepageSettingsUIConversion", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kHorizontalTabSwitcherAndroid{
    "HorizontalTabSwitcherAndroid", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kImmersiveUiMode{"ImmersiveUiMode",
                                     base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kInlineUpdateFlow{"InlineUpdateFlow",
                                      base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kInstantStart{"InstantStart",
                                  base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kKitKatSupported{"KitKatSupported",
                                     base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSearchEnginePromoExistingDevice{
    "SearchEnginePromo.ExistingDevice", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kSearchEnginePromoNewDevice{
    "SearchEnginePromo.NewDevice", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kNewPhotoPicker{"NewPhotoPicker",
                                    base::FEATURE_ENABLED_BY_DEFAULT};

// TODO(knollr): This is a temporary kill switch, it can be removed once we feel
// okay about leaving it on.
const base::Feature kNotificationSuspender{"NotificationSuspender",
                                           base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kOfflineIndicatorV2{"OfflineIndicatorV2",
                                        base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kOmniboxSpareRenderer{"OmniboxSpareRenderer",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kOverlayNewLayout{"OverlayNewLayout",
                                      base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kPageInfoPerformanceHints{
    "PageInfoPerformanceHints", base::FEATURE_DISABLED_BY_DEFAULT};

// TODO(rouslan): Remove this. (Currently used in
// GooglePayPaymentAppFactory.java)
const base::Feature kPayWithGoogleV1{"PayWithGoogleV1",
                                     base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kPhotoPickerVideoSupport{"PhotoPickerVideoSupport",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPhotoPickerZoom{"PhotoPickerZoom",
                                     base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kProbabilisticCryptidRenderer{
    "ProbabilisticCryptidRenderer", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kReachedCodeProfiler{"ReachedCodeProfiler",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kReaderModeInCCT{"ReaderModeInCCT",
                                     base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kRelatedSearches{"RelatedSearches",
                                     base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kRevampedContextMenu{"RevampedContextMenu",
                                         base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kServiceManagerForBackgroundPrefetch{
    "ServiceManagerForBackgroundPrefetch", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kServiceManagerForDownload{
    "ServiceManagerForDownload", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kShareButtonInTopToolbar{"ShareButtonInTopToolbar",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kShoppingAssist{"ShoppingAssist",
                                    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSpannableInlineAutocomplete{
    "SpannableInlineAutocomplete", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kSpecialLocaleWrapper{"SpecialLocaleWrapper",
                                          base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kSpecialUserDecision{"SpecialUserDecision",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSwapPixelFormatToFixConvertFromTranslucent{
    "SwapPixelFormatToFixConvertFromTranslucent",
    base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTabEngagementReportingAndroid{
    "TabEngagementReportingAndroid", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTabGroupsAndroid{"TabGroupsAndroid",
                                      base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTabGroupsContinuationAndroid{
    "TabGroupsContinuationAndroid", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTabGroupsUiImprovementsAndroid{
    "TabGroupsUiImprovementsAndroid", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTabGridLayoutAndroid{"TabGridLayoutAndroid",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTabReparenting{"TabReparenting",
                                    base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTabSwitcherOnReturn{"TabSwitcherOnReturn",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTabToGTSAnimation{"TabToGTSAnimation",
                                       base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTestDefaultDisabled{"TestDefaultDisabled",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTestDefaultEnabled{"TestDefaultEnabled",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTrustedWebActivityNewDisclosure{
    "TrustedWebActivityNewDisclosure", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTrustedWebActivityLocationDelegation{
    "TrustedWebActivityLocationDelegation", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTrustedWebActivityPostMessage{
    "TrustedWebActivityPostMessage", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kStartSurfaceAndroid{"StartSurfaceAndroid",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

// If enabled, keep logging and reporting UMA while chrome is backgrounded.
const base::Feature kUmaBackgroundSessions{"UMABackgroundSessions",
                                           base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kUpdateNotificationSchedulingIntegration{
    "UpdateNotificationSchedulingIntegration",
    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPrefetchNotificationSchedulingIntegration{
    "PrefetchNotificationSchedulingIntegration",
    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kUpdateNotificationScheduleServiceImmediateShowOption{
    "UpdateNotificationScheduleServiceImmediateShowOption",
    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kUsageStatsFeature{"UsageStats",
                                       base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kUserMediaScreenCapturing{
    "UserMediaScreenCapturing", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kVideoPersistence{"VideoPersistence",
                                      base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kVrBrowsingFeedback{"VrBrowsingFeedback",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kWebApkAdaptiveIcon{"WebApkAdaptiveIcon",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

static jboolean JNI_ChromeFeatureList_IsEnabled(
    JNIEnv* env,
    const JavaParamRef<jstring>& jfeature_name) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  return base::FeatureList::IsEnabled(*feature);
}

static ScopedJavaLocalRef<jstring>
JNI_ChromeFeatureList_GetFieldTrialParamByFeature(
    JNIEnv* env,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  const std::string& param_value =
      base::GetFieldTrialParamValueByFeature(*feature, param_name);
  return ConvertUTF8ToJavaString(env, param_value);
}

static jint JNI_ChromeFeatureList_GetFieldTrialParamByFeatureAsInt(
    JNIEnv* env,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name,
    const jint jdefault_value) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  return base::GetFieldTrialParamByFeatureAsInt(*feature, param_name,
                                                jdefault_value);
}

static jdouble JNI_ChromeFeatureList_GetFieldTrialParamByFeatureAsDouble(
    JNIEnv* env,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name,
    const jdouble jdefault_value) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  return base::GetFieldTrialParamByFeatureAsDouble(*feature, param_name,
                                                   jdefault_value);
}

static jboolean JNI_ChromeFeatureList_GetFieldTrialParamByFeatureAsBoolean(
    JNIEnv* env,
    const JavaParamRef<jstring>& jfeature_name,
    const JavaParamRef<jstring>& jparam_name,
    const jboolean jdefault_value) {
  const base::Feature* feature =
      FindFeatureExposedToJava(ConvertJavaStringToUTF8(env, jfeature_name));
  const std::string& param_name = ConvertJavaStringToUTF8(env, jparam_name);
  return base::GetFieldTrialParamByFeatureAsBool(*feature, param_name,
                                                 jdefault_value);
}

}  // namespace android
}  // namespace chrome
