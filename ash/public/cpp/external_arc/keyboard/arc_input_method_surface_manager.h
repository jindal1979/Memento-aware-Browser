// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PUBLIC_CPP_EXTERNAL_ARC_KEYBOARD_ARC_INPUT_METHOD_SURFACE_MANAGER_H_
#define ASH_PUBLIC_CPP_EXTERNAL_ARC_KEYBOARD_ARC_INPUT_METHOD_SURFACE_MANAGER_H_

#include "ash/public/cpp/ash_public_export.h"
#include "ash/public/cpp/keyboard/arc/arc_input_method_bounds_tracker.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "components/exo/input_method_surface_manager.h"
#include "ui/gfx/geometry/rect.h"

namespace ash {

class ASH_PUBLIC_EXPORT ArcInputMethodSurfaceManager
    : public exo::InputMethodSurfaceManager,
      public ArcInputMethodBoundsTracker {
 public:
  ArcInputMethodSurfaceManager();
  ~ArcInputMethodSurfaceManager() override;

  // exo::InputMethodSurfaceManager:
  exo::InputMethodSurface* GetSurface() const override;
  void AddSurface(exo::InputMethodSurface* surface) override;
  void RemoveSurface(exo::InputMethodSurface* surface) override;
  void OnTouchableBoundsChanged(exo::InputMethodSurface* surface) override;

 private:
  exo::InputMethodSurface* input_method_surface_ = nullptr;  // Not owned

  DISALLOW_COPY_AND_ASSIGN(ArcInputMethodSurfaceManager);
};

}  // namespace ash

#endif  // ASH_PUBLIC_CPP_EXTERNAL_ARC_KEYBOARD_ARC_INPUT_METHOD_SURFACE_MANAGER_H_
