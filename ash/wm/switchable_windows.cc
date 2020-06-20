// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/switchable_windows.h"

#include <array>

#include "ash/public/cpp/ash_features.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/wm/desks/desks_util.h"
#include "base/stl_util.h"
#include "ui/aura/window.h"

namespace ash {

namespace {

constexpr std::array<int, 6> kSwitchableContainers = {
    kShellWindowId_DefaultContainerDeprecated,
    kShellWindowId_DeskContainerB,
    kShellWindowId_DeskContainerC,
    kShellWindowId_DeskContainerD,
    kShellWindowId_AlwaysOnTopContainer,
    kShellWindowId_PipContainer,
};

}  // namespace

std::vector<aura::Window*> GetSwitchableContainersForRoot(
    aura::Window* root,
    bool active_desk_only) {
  DCHECK(root);
  DCHECK(root->IsRootWindow());

  std::vector<aura::Window*> containers;
  if (active_desk_only) {
    containers.push_back(desks_util::GetActiveDeskContainerForRoot(root));
    containers.push_back(
        root->GetChildById(kShellWindowId_AlwaysOnTopContainer));
    return containers;
  }

  for (const auto& id : kSwitchableContainers) {
    auto* container = root->GetChildById(id);
    DCHECK(container);
    containers.push_back(container);
  }

  return containers;
}

// TODO(afakhry): Rename this to a better name.
bool IsSwitchableContainer(const aura::Window* window) {
  if (!window)
    return false;
  const int shell_window_id = window->id();

  return base::Contains(kSwitchableContainers, shell_window_id);
}

}  // namespace ash
