// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_UNIFIED_DETAILED_VIEW_CONTROLLER_H_
#define ASH_SYSTEM_UNIFIED_DETAILED_VIEW_CONTROLLER_H_

#include "base/strings/string16.h"

namespace views {
class View;
}  // namespace views

namespace ash {

// Base class for controllers of detailed views.
// To add a new detailed view, implement this class, and instantiate in
// UnifiedSystemTrayController::Show*DetailedView() method.
class DetailedViewController {
 public:
  virtual ~DetailedViewController() = default;

  // Create the detailed view. The view will be owned by views hierarchy. The
  // view will be always deleted after the controller is destructed.
  virtual views::View* CreateView() = 0;
  virtual base::string16 GetAccessibleName() const = 0;
};

}  // namespace ash

#endif  // ASH_SYSTEM_UNIFIED_DETAILED_VIEW_CONTROLLER_H_
