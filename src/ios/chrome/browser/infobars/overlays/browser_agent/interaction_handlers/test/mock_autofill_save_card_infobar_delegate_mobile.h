// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_TEST_MOCK_AUTOFILL_SAVE_CARD_INFOBAR_DELEGATE_MOBILE_H_
#define IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_TEST_MOCK_AUTOFILL_SAVE_CARD_INFOBAR_DELEGATE_MOBILE_H_

#include <memory>
#include <string>

#include "components/autofill/core/browser/payments/autofill_save_card_infobar_delegate_mobile.h"

#include "base/strings/string16.h"
#include "components/autofill/core/browser/autofill_client.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/browser/payments/legal_message_line.h"
#include "components/prefs/pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"

class GURL;

class MockAutofillSaveCardInfoBarDelegateMobile
    : public autofill::AutofillSaveCardInfoBarDelegateMobile {
 public:
  MockAutofillSaveCardInfoBarDelegateMobile(
      bool upload,
      autofill::AutofillClient::SaveCreditCardOptions options,
      const autofill::CreditCard& card,
      const autofill::LegalMessageLines& legal_message_lines,
      autofill::AutofillClient::UploadSaveCardPromptCallback
          upload_save_card_prompt_callback,
      autofill::AutofillClient::LocalSaveCardPromptCallback
          local_save_card_prompt_callback,
      PrefService* pref_service);
  ~MockAutofillSaveCardInfoBarDelegateMobile() override;

  MOCK_METHOD3(UpdateAndAccept,
               bool(base::string16 cardholder_name,
                    base::string16 expiration_date_month,
                    base::string16 expiration_date_year));
  MOCK_METHOD1(OnLegalMessageLinkClicked, void(GURL url));
};

class MockAutofillSaveCardInfoBarDelegateMobileFactory {
 public:
  MockAutofillSaveCardInfoBarDelegateMobileFactory();
  ~MockAutofillSaveCardInfoBarDelegateMobileFactory();

  static std::unique_ptr<MockAutofillSaveCardInfoBarDelegateMobile>
  CreateMockAutofillSaveCardInfoBarDelegateMobileFactory(
      bool upload,
      PrefService* service,
      autofill::CreditCard card);

 private:
  std::unique_ptr<PrefService> prefs_;
  autofill::CreditCard credit_card_;
};

#endif  // IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_TEST_MOCK_AUTOFILL_SAVE_CARD_INFOBAR_DELEGATE_MOBILE_H_
