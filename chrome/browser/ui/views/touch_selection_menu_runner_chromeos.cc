// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/touch_selection_menu_runner_chromeos.h"

#include <utility>

#include "base/bind.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/views/touch_selection_menu_chromeos.h"
#include "components/arc/arc_features.h"
#include "components/arc/arc_service_manager.h"
#include "components/arc/session/arc_bridge_service.h"
#include "ui/aura/window.h"
#include "ui/base/layout.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"

TouchSelectionMenuRunnerChromeOS::TouchSelectionMenuRunnerChromeOS() = default;

TouchSelectionMenuRunnerChromeOS::~TouchSelectionMenuRunnerChromeOS() = default;

void TouchSelectionMenuRunnerChromeOS::OpenMenuWithTextSelectionAction(
    ui::TouchSelectionMenuClient* client,
    const gfx::Rect& anchor_rect,
    const gfx::Size& handle_image_size,
    std::unique_ptr<aura::WindowTracker> tracker,
    std::vector<arc::mojom::TextSelectionActionPtr> actions) {
  if (tracker->windows().empty())
    return;
  if (!client->ShouldShowQuickMenu())
    return;

  arc::mojom::TextSelectionActionPtr top_action;
  // Get the first action generated by the Android TextClassifier.
  for (auto& action : actions) {
    if (!action->text_classifier_action)
      continue;
    std::swap(top_action, action);
    break;
  }

  // The menu manages its own lifetime and deletes itself when closed.
  TouchSelectionMenuChromeOS* menu = new TouchSelectionMenuChromeOS(
      this, client, tracker->Pop(), std::move(top_action));
  ShowMenu(menu, anchor_rect, handle_image_size);
}

bool TouchSelectionMenuRunnerChromeOS::RequestTextSelection(
    ui::TouchSelectionMenuClient* client,
    const gfx::Rect& anchor_rect,
    const gfx::Size& handle_image_size,
    aura::Window* context) {
  if (!base::FeatureList::IsEnabled(arc::kSmartTextSelectionFeature))
    return false;

  const std::string converted_text =
      base::UTF16ToUTF8(client->GetSelectedText());
  if (converted_text.empty())
    return false;

  auto* arc_service_manager = arc::ArcServiceManager::Get();
  if (!arc_service_manager)
    return false;

  arc::mojom::IntentHelperInstance* instance = ARC_GET_INSTANCE_FOR_METHOD(
      arc_service_manager->arc_bridge_service()->intent_helper(),
      RequestTextSelectionActions);
  if (!instance)
    return false;

  // aura::WindowTracker is used since the newly created menu may need to know
  // about the parent window.
  std::unique_ptr<aura::WindowTracker> tracker =
      std::make_unique<aura::WindowTracker>();
  tracker->Add(context);

  const display::Screen* screen = display::Screen::GetScreen();
  DCHECK(screen);

  base::RecordAction(base::UserMetricsAction("Arc.SmartTextSelection.Request"));
  // Fetch actions for selected text and then show quick menu.
  instance->RequestTextSelectionActions(
      converted_text,
      arc::mojom::ScaleFactor(
          screen->GetDisplayNearestWindow(context).device_scale_factor()),
      base::BindOnce(
          &TouchSelectionMenuRunnerChromeOS::OpenMenuWithTextSelectionAction,
          weak_ptr_factory_.GetWeakPtr(), client, anchor_rect,
          handle_image_size, std::move(tracker)));
  return true;
}

void TouchSelectionMenuRunnerChromeOS::OpenMenu(
    ui::TouchSelectionMenuClient* client,
    const gfx::Rect& anchor_rect,
    const gfx::Size& handle_image_size,
    aura::Window* context) {
  views::TouchSelectionMenuRunnerViews::CloseMenu();

  // If there are no commands to show in the menu finish right away. Also if
  // classification is possible delegate creating/showing a new menu.
  if (!views::TouchSelectionMenuRunnerViews::IsMenuAvailable(client) ||
      RequestTextSelection(client, anchor_rect, handle_image_size, context)) {
    return;
  }

  // The menu manages its own lifetime and deletes itself when closed.
  TouchSelectionMenuChromeOS* menu =
      new TouchSelectionMenuChromeOS(this, client, context,
                                     /*action=*/nullptr);
  ShowMenu(menu, anchor_rect, handle_image_size);
}
