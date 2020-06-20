// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_HOST_ASH_WINDOW_TREE_HOST_MIRRORING_UNIFIED_H_
#define ASH_HOST_ASH_WINDOW_TREE_HOST_MIRRORING_UNIFIED_H_

#include "ash/host/ash_window_tree_host_platform.h"

namespace ash {

class AshWindowTreeHostMirroringDelegate;

// A window tree host for the mirroing displays that constitute the unified
// desktop. This correctly handles coordinates conversion from DIP to pixels and
// vice versa.
class AshWindowTreeHostMirroringUnified : public AshWindowTreeHostPlatform {
 public:
  AshWindowTreeHostMirroringUnified(
      const gfx::Rect& initial_bounds,
      int64_t mirroring_display_id,
      AshWindowTreeHostMirroringDelegate* delegate);
  ~AshWindowTreeHostMirroringUnified() override;

  // aura::WindowTreeHost:
  gfx::Transform GetRootTransformForLocalEventCoordinates() const override;
  void ConvertDIPToPixels(gfx::Point* point) const override;
  void ConvertPixelsToDIP(gfx::Point* point) const override;

  // ash::AshWindowTreeHostPlatform:
  void PrepareForShutdown() override;

  // ui::PlatformWindowDelegate:
  void OnMouseEnter() override;

 private:
  int64_t mirroring_display_id_;

  AshWindowTreeHostMirroringDelegate* delegate_;  // Not owned.

  bool is_shutting_down_ = false;

  DISALLOW_COPY_AND_ASSIGN(AshWindowTreeHostMirroringUnified);
};

}  // namespace ash

#endif  // ASH_HOST_ASH_WINDOW_TREE_HOST_MIRRORING_UNIFIED_H_
