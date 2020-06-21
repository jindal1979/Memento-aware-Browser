// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_JAVASCRIPT_DIALOGS_TAB_MODAL_DIALOG_VIEW_H_
#define COMPONENTS_JAVASCRIPT_DIALOGS_TAB_MODAL_DIALOG_VIEW_H_

#include "base/strings/string16.h"

namespace javascript_dialogs {

class TabModalDialogView {
 public:
  virtual ~TabModalDialogView() {}

  // Closes the dialog without sending a callback. This is useful when the
  // TabModalDialogManager needs to make this dialog go away so that it can
  // respond to a call that requires it to make no callback or make a customized
  // one.
  virtual void CloseDialogWithoutCallback() = 0;

  // Returns the current value of the user input for a prompt dialog.
  virtual base::string16 GetUserInput() = 0;
};

}  // namespace javascript_dialogs

#endif  // COMPONENTS_JAVASCRIPT_DIALOGS_TAB_MODAL_DIALOG_VIEW_H_
