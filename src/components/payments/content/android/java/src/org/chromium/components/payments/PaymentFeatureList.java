// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.payments;

import org.chromium.base.FeatureList;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;

/**
 * Exposes payment specific features in to java since files in org.chromium.components.payments
 * package package cannot depend on
 * org.chromium.chrome.browser.flags.org.chromium.chrome.browser.flags.ChromeFeatureList.
 */
@JNINamespace("payments::android")
public class PaymentFeatureList {
    /** Alphabetical: */
    public static final String ANDROID_APP_PAYMENT_UPDATE_EVENTS = "AndroidAppPaymentUpdateEvents";
    public static final String PAYMENT_REQUEST_SKIP_TO_GPAY = "PaymentRequestSkipToGPay";
    public static final String PAYMENT_REQUEST_SKIP_TO_GPAY_IF_NO_CARD =
            "PaymentRequestSkipToGPayIfNoCard";
    public static final String SCROLL_TO_EXPAND_PAYMENT_HANDLER = "ScrollToExpandPaymentHandler";
    public static final String SERVICE_WORKER_PAYMENT_APPS = "ServiceWorkerPaymentApps";
    public static final String STRICT_HAS_ENROLLED_AUTOFILL_INSTRUMENT =
            "StrictHasEnrolledAutofillInstrument";
    public static final String WEB_PAYMENTS = "WebPayments";
    public static final String WEB_PAYMENTS_ALWAYS_ALLOW_JUST_IN_TIME_PAYMENT_APP =
            "AlwaysAllowJustInTimePaymentApp";
    public static final String WEB_PAYMENTS_APP_STORE_BILLING_DEBUG = "AppStoreBillingDebug";
    public static final String WEB_PAYMENTS_EXPERIMENTAL_FEATURES =
            "WebPaymentsExperimentalFeatures";
    public static final String WEB_PAYMENTS_METHOD_SECTION_ORDER_V2 =
            "WebPaymentsMethodSectionOrderV2";
    public static final String WEB_PAYMENTS_MINIMAL_UI = "WebPaymentsMinimalUI";
    public static final String WEB_PAYMENTS_MODIFIERS = "WebPaymentsModifiers";
    public static final String WEB_PAYMENTS_REDACT_SHIPPING_ADDRESS =
            "WebPaymentsRedactShippingAddress";
    public static final String WEB_PAYMENTS_RETURN_GOOGLE_PAY_IN_BASIC_CARD =
            "ReturnGooglePayInBasicCard";
    public static final String WEB_PAYMENTS_SINGLE_APP_UI_SKIP = "WebPaymentsSingleAppUiSkip";

    // Do not instantiate this class.
    private PaymentFeatureList() {}

    /**
     * Returns whether the specified feature is enabled or not.
     *
     * Note: Features queried through this API must be added to the array
     * |kFeaturesExposedToJava| in components/payments/content/android/payment_feature_list.cc
     *
     * @param featureName The name of the feature to query.
     * @return Whether the feature is enabled or not.
     */
    public static boolean isEnabled(String featureName) {
        assert FeatureList.isNativeInitialized();
        return PaymentFeatureListJni.get().isEnabled(featureName);
    }

    /**
     * Returns whether the feature is enabled or not. *
     * Note: Features queried through this API must be added to the array
     * |kFeaturesExposedToJava| in components/payments/content/android/payment_feature_list.cc
     *
     * @param featureName The name of the feature to query.
     * @return true when either the specified feature or |WEB_PAYMENTS_EXPERIMENTAL_FEATURES| is
     *         enabled.
     */
    public static boolean isEnabledOrExperimentalFeaturesEnabled(String featureName) {
        return isEnabled(WEB_PAYMENTS_EXPERIMENTAL_FEATURES) || isEnabled(featureName);
    }

    /**
     * The interface implemented by the automatically generated JNI bindings class
     * PaymentsFeatureListJni.
     */
    @NativeMethods
    /* package */ interface Natives {
        boolean isEnabled(String featureName);
    }
}
