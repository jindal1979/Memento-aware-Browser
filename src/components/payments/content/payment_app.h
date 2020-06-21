// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_PAYMENT_APP_H_
#define COMPONENTS_PAYMENTS_CONTENT_PAYMENT_APP_H_

#include <set>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/payments/core/payer_data.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "third_party/blink/public/mojom/payments/payment_app.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace payments {

class PaymentHandlerHost;

// Base class which represents a payment app in Payment Request.
class PaymentApp {
 public:
  // The type of this app instance.
  // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.components.payments
  // GENERATED_JAVA_CLASS_NAME_OVERRIDE: PaymentAppType
  enum class Type {
    // Undefined type of payment app. Can be used for setting the default return
    // value of an abstract class or an interface.
    UNDEFINED,
    // The payment app built into the browser that uses the autofill data.
    AUTOFILL,
    // A 3rd-party platform-specific mobile app, such as an Android app
    // integrated via
    // https://developers.google.com/web/fundamentals/payments/payment-apps-developer-guide/android-payment-apps
    NATIVE_MOBILE_APP,
    // A 3rd-party cross-platform service worked based payment app.
    SERVICE_WORKER_APP,
    // An internal 1st-party payment app, e.g., Google Pay on Chrome or Samsung
    // Pay on Samsung Internet.
    INTERNAL,
  };

  class Delegate {
   public:
    virtual ~Delegate() {}

    // Should be called with method name (e.g., "https://google.com/pay") and
    // json-serialized stringified details.
    virtual void OnInstrumentDetailsReady(
        const std::string& method_name,
        const std::string& stringified_details,
        const PayerData& payer_data) = 0;

    // Should be called with a developer-facing error message to be used when
    // rejecting PaymentRequest.show().
    virtual void OnInstrumentDetailsError(const std::string& error_message) = 0;
  };

  virtual ~PaymentApp();

  // Will call into the |delegate| (can't be null) on success or error.
  virtual void InvokePaymentApp(Delegate* delegate) = 0;
  // Called when the payment app window has closed.
  virtual void OnPaymentAppWindowClosed() {}
  // Returns whether the app is complete to be used for payment without further
  // editing.
  virtual bool IsCompleteForPayment() const = 0;
  // Returns the calculated completeness score. Used to sort the list of
  // available apps.
  virtual uint32_t GetCompletenessScore() const = 0;
  // Returns whether the app can be preselected in the payment sheet. If none of
  // the apps can be preselected, the user must explicitly select an app from a
  // list.
  virtual bool CanPreselect() const = 0;
  // Returns a message to indicate to the user what's missing for the app to be
  // complete for payment.
  virtual base::string16 GetMissingInfoLabel() const = 0;
  // Returns this app's answer for PaymentRequest.hasEnrolledInstrument().
  virtual bool HasEnrolledInstrument() const = 0;
  // Records the use of this payment app.
  virtual void RecordUse() = 0;
  // Check whether this payment app needs installation before it can be used.
  virtual bool NeedsInstallation() const = 0;

  // The non-human readable identifier for this payment app. For example, the
  // GUID of an autofill card or the scope of a payment handler.
  virtual std::string GetId() const = 0;

  // Return the sub/label of payment app, to be displayed to the user.
  virtual base::string16 GetLabel() const = 0;
  virtual base::string16 GetSublabel() const = 0;

  // Returns the icon bitmap or null.
  virtual const SkBitmap* icon_bitmap() const;

  // Returns the identifier for another payment app that should be hidden when
  // this payment app is present.
  virtual std::string GetApplicationIdentifierToHide() const;

  // Returns the set of identifier of other apps that would cause this app to be
  // hidden, if any of them are present, e.g., ["com.bobpay.production",
  // "com.bobpay.beta"].
  virtual std::set<std::string> GetApplicationIdentifiersThatHideThisApp()
      const;

  // Whether the payment app is ready for minimal UI flow.
  virtual bool IsReadyForMinimalUI() const;

  // The account balance of the payment app that is ready for a minimal UI flow.
  virtual std::string GetAccountBalance() const;

  // Disable opening a window for this payment app. Used in minimal UI flow.
  virtual void DisableShowingOwnUI();

  // Returns true if this payment app can be used to fulfill a request
  // specifying |method| as supported method of payment. The parsed basic-card
  // specific data (supported_networks) is relevant only for the
  // AutofillPaymentApp, which runs inside of the browser process and thus
  // should not be parsing untrusted JSON strings from the renderer.
  virtual bool IsValidForModifier(
      const std::string& method,
      bool supported_networks_specified,
      const std::set<std::string>& supported_networks) const = 0;

  // Sets |is_valid| to true if this payment app can handle payments for the
  // given |payment_method_identifier|. The |is_valid| is an out-param instead
  // of the return value to enable binding this method with a base::WeakPtr,
  // which prohibits non-void methods.
  void IsValidForPaymentMethodIdentifier(
      const std::string& payment_method_identifier,
      bool* is_valid) const;

  // Returns a WeakPtr to this payment app.
  virtual base::WeakPtr<PaymentApp> AsWeakPtr() = 0;

  // Returns true if this payment app can collect and return the required
  // information. This is used to show/hide shipping/contact sections in payment
  // sheet view depending on the selected app.
  virtual bool HandlesShippingAddress() const = 0;
  virtual bool HandlesPayerName() const = 0;
  virtual bool HandlesPayerEmail() const = 0;
  virtual bool HandlesPayerPhone() const = 0;

  // Returns the set of payment methods supported by this app.
  const std::set<std::string>& GetAppMethodNames() const;

  // Sorts the apps using the overloaded < operator.
  static void SortApps(std::vector<std::unique_ptr<PaymentApp>>* apps);
  static void SortApps(std::vector<PaymentApp*>* apps);

  int icon_resource_id() const { return icon_resource_id_; }
  Type type() const { return type_; }

  virtual ukm::SourceId UkmSourceId();

  // Optionally bind to the Mojo pipe for receiving events generated by the
  // invoked payment handler.
  virtual void SetPaymentHandlerHost(
      base::WeakPtr<PaymentHandlerHost> payment_handler_host) {}

  // Whether the payment app is waiting for the merchant to update the purchase
  // price based on the shipping, billing, or contact information that the user
  // has selected inside of the payment app.
  virtual bool IsWaitingForPaymentDetailsUpdate() const;

  // Notifies the payment app of the updated details, such as updated total, in
  // response to the change of any of the following: payment method, shipping
  // address, or shipping option.
  virtual void UpdateWith(
      mojom::PaymentRequestDetailsUpdatePtr details_update) {}

  // Notifies the payment app that the merchant did not handle the payment
  // method, shipping option, or shipping address change events, so the payment
  // details are unchanged.
  virtual void OnPaymentDetailsNotUpdated() {}

  // Requests the invoked payment app to abort if possible. Only called if this
  // payment app is currently invoked.
  virtual void AbortPaymentApp(base::OnceCallback<void(bool)> abort_callback);

 protected:
  PaymentApp(int icon_resource_id, Type type);

  // The set of payment methods supported by this app.
  std::set<std::string> app_method_names_;

 private:
  bool operator<(const PaymentApp& other) const;
  int icon_resource_id_;
  Type type_;

  DISALLOW_COPY_AND_ASSIGN(PaymentApp);
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_PAYMENT_APP_H_
