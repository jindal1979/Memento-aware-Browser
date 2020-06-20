// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/accessibility/autoclick_menu_bubble_controller.h"

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shelf/shelf.h"
#include "ash/shell.h"
#include "ash/system/accessibility/autoclick_scroll_bubble_controller.h"
#include "ash/system/accessibility/floating_menu_utils.h"
#include "ash/system/tray/tray_background_view.h"
#include "ash/system/tray/tray_bubble_view.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/unified/unified_system_tray_view.h"
#include "ash/wm/collision_detection/collision_detection_utils.h"
#include "ash/wm/work_area_insets.h"
#include "ash/wm/workspace/workspace_layout_manager.h"
#include "ash/wm/workspace_controller.h"
#include "ui/aura/window_tree_host.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event_utils.h"
#include "ui/views/bubble/bubble_border.h"

namespace ash {

namespace {
// Autoclick menu constants.
const int kAutoclickMenuWidth = 369;
const int kAutoclickMenuHeight = 64;
}  // namespace

AutoclickMenuBubbleController::AutoclickMenuBubbleController() {
  Shell::Get()->locale_update_controller()->AddObserver(this);
}

AutoclickMenuBubbleController::~AutoclickMenuBubbleController() {
  if (bubble_widget_ && !bubble_widget_->IsClosed())
    bubble_widget_->CloseNow();
  Shell::Get()->locale_update_controller()->RemoveObserver(this);
  scroll_bubble_controller_.reset();
}

void AutoclickMenuBubbleController::SetEventType(AutoclickEventType type) {
  if (menu_view_)
    menu_view_->UpdateEventType(type);

  if (type == AutoclickEventType::kScroll) {
    // If the type is scroll, show the scroll bubble using the
    // scroll_bubble_controller_.
    if (!scroll_bubble_controller_) {
      scroll_bubble_controller_ =
          std::make_unique<AutoclickScrollBubbleController>();
    }
    gfx::Rect anchor_rect = bubble_view_->GetBoundsInScreen();
    anchor_rect.Inset(-kCollisionWindowWorkAreaInsetsDp,
                      -kCollisionWindowWorkAreaInsetsDp);
    scroll_bubble_controller_->ShowBubble(
        anchor_rect, GetAnchorAlignmentForFloatingMenuPosition(position_));
  } else if (scroll_bubble_controller_) {
    scroll_bubble_controller_ = nullptr;
    // Update the bubble menu's position in case it had moved out of the way
    // for the scroll bubble.
    SetPosition(position_);
  }
}

void AutoclickMenuBubbleController::SetPosition(
    FloatingMenuPosition new_position) {
  if (!menu_view_ || !bubble_view_ || !bubble_widget_)
    return;

  // Update the menu view's UX if the position has changed, or if it's not the
  // default position (because that can change with language direction).
  if (position_ != new_position ||
      new_position == FloatingMenuPosition::kSystemDefault) {
    menu_view_->UpdatePosition(new_position);
  }
  position_ = new_position;

  // If this is the default system position, pick the position based on the
  // language direction.
  if (new_position == FloatingMenuPosition::kSystemDefault)
    new_position = DefaultSystemFloatingMenuPosition();

  // TODO(katie): Support multiple displays: draw the menu on whichever display
  // the cursor is on.
  gfx::Rect new_bounds = GetOnScreenBoundsForFloatingMenuPosition(
      gfx::Size(kAutoclickMenuWidth, kAutoclickMenuHeight), new_position);

  // Update the preferred bounds based on other system windows.
  gfx::Rect resting_bounds = CollisionDetectionUtils::GetRestingPosition(
      display::Screen::GetScreen()->GetDisplayNearestWindow(
          bubble_widget_->GetNativeWindow()),
      new_bounds,
      CollisionDetectionUtils::RelativePriority::kAutomaticClicksMenu);

  // Un-inset the bounds to get the widget's bounds, which includes the drop
  // shadow.
  resting_bounds.Inset(-kCollisionWindowWorkAreaInsetsDp, 0,
                       -kCollisionWindowWorkAreaInsetsDp,
                       -kCollisionWindowWorkAreaInsetsDp);
  if (bubble_widget_->GetWindowBoundsInScreen() == resting_bounds)
    return;

  ui::ScopedLayerAnimationSettings settings(
      bubble_widget_->GetLayer()->GetAnimator());
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kAnimationDurationMs));
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  bubble_widget_->SetBounds(resting_bounds);

  if (!scroll_bubble_controller_)
    return;

  // Position the scroll bubble with respect to the menu.
  scroll_bubble_controller_->UpdateAnchorRect(
      resting_bounds, GetAnchorAlignmentForFloatingMenuPosition(new_position));
}

void AutoclickMenuBubbleController::SetScrollPosition(
    gfx::Rect scroll_bounds_in_dips,
    const gfx::Point& scroll_point_in_dips) {
  if (!scroll_bubble_controller_)
    return;
  scroll_bubble_controller_->SetScrollPosition(scroll_bounds_in_dips,
                                               scroll_point_in_dips);
}

void AutoclickMenuBubbleController::ShowBubble(AutoclickEventType type,
                                               FloatingMenuPosition position) {
  // Ignore if bubble widget already exists.
  if (bubble_widget_)
    return;

  DCHECK(!bubble_view_);

  TrayBubbleView::InitParams init_params;
  init_params.delegate = this;
  // Anchor within the overlay container.
  init_params.parent_window =
      Shell::GetContainer(Shell::GetPrimaryRootWindow(),
                          kShellWindowId_AccessibilityBubbleContainer);
  init_params.anchor_mode = TrayBubbleView::AnchorMode::kRect;
  init_params.is_anchored_to_status_area = false;
  // The widget's shadow is drawn below and on the sides of the view, with a
  // width of kCollisionWindowWorkAreaInsetsDp. Set the top inset to 0 to ensure
  // the scroll view is drawn at kCollisionWindowWorkAreaInsetsDp above the
  // bubble menu when the position is at the bottom of the screen. The space
  // between the bubbles belongs to the scroll view bubble's shadow.
  init_params.insets = gfx::Insets(0, kCollisionWindowWorkAreaInsetsDp,
                                   kCollisionWindowWorkAreaInsetsDp,
                                   kCollisionWindowWorkAreaInsetsDp);
  init_params.preferred_width = kAutoclickMenuWidth;
  init_params.corner_radius = kUnifiedTrayCornerRadius;
  init_params.has_shadow = false;
  init_params.translucent = true;
  bubble_view_ = new TrayBubbleView(init_params);

  menu_view_ = new AutoclickMenuView(type, position);
  menu_view_->SetBorder(
      views::CreateEmptyBorder(kUnifiedTopShortcutSpacing, 0, 0, 0));
  bubble_view_->AddChildView(menu_view_);

  menu_view_->SetPaintToLayer();
  menu_view_->layer()->SetFillsBoundsOpaquely(false);

  bubble_widget_ = views::BubbleDialogDelegateView::CreateBubble(bubble_view_);
  TrayBackgroundView::InitializeBubbleAnimations(bubble_widget_);
  CollisionDetectionUtils::MarkWindowPriorityForCollisionDetection(
      bubble_widget_->GetNativeWindow(),
      CollisionDetectionUtils::RelativePriority::kAutomaticClicksMenu);
  bubble_view_->InitializeAndShowBubble();

  SetPosition(position);
}

void AutoclickMenuBubbleController::CloseBubble() {
  if (!bubble_widget_ || bubble_widget_->IsClosed())
    return;
  bubble_widget_->Close();
}

void AutoclickMenuBubbleController::SetBubbleVisibility(bool is_visible) {
  if (!bubble_widget_)
    return;

  if (is_visible)
    bubble_widget_->Show();
  else
    bubble_widget_->Hide();

  if (!scroll_bubble_controller_)
    return;
  scroll_bubble_controller_->SetBubbleVisibility(is_visible);
}

void AutoclickMenuBubbleController::ClickOnBubble(gfx::Point location_in_dips,
                                                  int mouse_event_flags) {
  if (!bubble_widget_)
    return;

  // Change the event location bounds to be relative to the menu bubble.
  location_in_dips -= bubble_view_->GetBoundsInScreen().OffsetFromOrigin();

  // Generate synthesized mouse events for the click.
  const ui::MouseEvent press_event(ui::ET_MOUSE_PRESSED, location_in_dips,
                                   location_in_dips, ui::EventTimeForNow(),
                                   mouse_event_flags | ui::EF_LEFT_MOUSE_BUTTON,
                                   ui::EF_LEFT_MOUSE_BUTTON);
  const ui::MouseEvent release_event(
      ui::ET_MOUSE_RELEASED, location_in_dips, location_in_dips,
      ui::EventTimeForNow(), mouse_event_flags | ui::EF_LEFT_MOUSE_BUTTON,
      ui::EF_LEFT_MOUSE_BUTTON);

  // Send the press/release events to the widget's root view for processing.
  bubble_widget_->GetRootView()->OnMousePressed(press_event);
  bubble_widget_->GetRootView()->OnMouseReleased(release_event);
}

void AutoclickMenuBubbleController::ClickOnScrollBubble(
    gfx::Point location_in_dips,
    int mouse_event_flags) {
  if (!scroll_bubble_controller_)
    return;

  scroll_bubble_controller_->ClickOnBubble(location_in_dips, mouse_event_flags);
}

bool AutoclickMenuBubbleController::ContainsPointInScreen(
    const gfx::Point& point) {
  return bubble_view_ && bubble_view_->GetBoundsInScreen().Contains(point);
}

bool AutoclickMenuBubbleController::ScrollBubbleContainsPointInScreen(
    const gfx::Point& point) {
  return scroll_bubble_controller_ &&
         scroll_bubble_controller_->ContainsPointInScreen(point);
}

void AutoclickMenuBubbleController::BubbleViewDestroyed() {
  bubble_view_ = nullptr;
  bubble_widget_ = nullptr;
  menu_view_ = nullptr;
}

void AutoclickMenuBubbleController::OnLocaleChanged() {
  // Layout update is needed when language changes between LTR and RTL, if the
  // position is the system default.
  if (position_ == FloatingMenuPosition::kSystemDefault)
    SetPosition(position_);
}

}  // namespace ash
