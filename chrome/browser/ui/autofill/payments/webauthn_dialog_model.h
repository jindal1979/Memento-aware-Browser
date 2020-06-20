// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_WEBAUTHN_DIALOG_MODEL_H_
#define CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_WEBAUTHN_DIALOG_MODEL_H_

#include "base/macros.h"
#include "base/observer_list.h"
#include "chrome/browser/ui/webauthn/authenticator_request_sheet_model.h"
#include "ui/gfx/vector_icon_types.h"

namespace autofill {

class WebauthnDialogModelObserver;
enum class WebauthnDialogState;

// The model for WebauthnDialogView determining what content is shown.
// Owned by the AuthenticatorRequestSheetView.
class WebauthnDialogModel : public AuthenticatorRequestSheetModel {
 public:
  explicit WebauthnDialogModel(WebauthnDialogState dialog_state);
  ~WebauthnDialogModel() override;

  // Update the current state the dialog should be. When the state is changed,
  // the view's contents should be re-initialized. This should not be used
  // before the view is created.
  void SetDialogState(WebauthnDialogState state);
  WebauthnDialogState dialog_state() { return state_; }

  void AddObserver(WebauthnDialogModelObserver* observer);
  void RemoveObserver(WebauthnDialogModelObserver* observer);

  // AuthenticatorRequestSheetModel:
  bool IsActivityIndicatorVisible() const override;
  bool IsBackButtonVisible() const override;
  bool IsCancelButtonVisible() const override;
  base::string16 GetCancelButtonLabel() const override;
  bool IsAcceptButtonVisible() const override;
  bool IsAcceptButtonEnabled() const override;
  base::string16 GetAcceptButtonLabel() const override;
  const gfx::VectorIcon& GetStepIllustration(
      ImageColorScheme color_scheme) const override;
  base::string16 GetStepTitle() const override;
  base::string16 GetStepDescription() const override;
  // Event handling is handed over to the controller.
  void OnBack() override {}
  void OnAccept() override {}
  void OnCancel() override {}

 private:
  WebauthnDialogState state_;

  base::ObserverList<WebauthnDialogModelObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(WebauthnDialogModel);
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_PAYMENTS_WEBAUTHN_DIALOG_MODEL_H_
