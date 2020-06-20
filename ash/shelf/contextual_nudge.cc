// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/contextual_nudge.h"

#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shelf/shelf.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/wm/collision_detection/collision_detection_utils.h"
#include "ui/aura/window.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/color_palette.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"

namespace ash {
namespace {

views::BubbleBorder::Arrow GetArrowForPosition(
    ContextualNudge::Position position) {
  switch (position) {
    case ContextualNudge::Position::kTop:
      return views::BubbleBorder::BOTTOM_CENTER;
    case ContextualNudge::Position::kBottom:
      return views::BubbleBorder::TOP_CENTER;
  }
}

}  // namespace

ContextualNudge::ContextualNudge(views::View* anchor,
                                 aura::Window* parent_window,
                                 Position position,
                                 const gfx::Insets& margins,
                                 const base::string16& text,
                                 SkColor text_color,
                                 const base::RepeatingClosure& tap_callback)
    : views::BubbleDialogDelegateView(anchor,
                                      GetArrowForPosition(position),
                                      views::BubbleBorder::NO_ASSETS),
      tap_callback_(tap_callback) {
  set_color(SK_ColorTRANSPARENT);
  set_close_on_deactivate(false);
  set_margins(gfx::Insets());
  set_accept_events(!tap_callback.is_null());
  SetCanActivate(false);
  set_adjust_if_offscreen(false);
  set_shadow(views::BubbleBorder::NO_ASSETS);
  SetButtons(ui::DIALOG_BUTTON_NONE);

  if (parent_window) {
    set_parent_window(parent_window);
  } else if (anchor_widget()) {
    set_parent_window(
        anchor_widget()->GetNativeWindow()->GetRootWindow()->GetChildById(
            kShellWindowId_ShelfContainer));
  }

  SetLayoutManager(std::make_unique<views::FillLayout>());

  label_ = AddChildView(std::make_unique<views::Label>(text));
  label_->SetPaintToLayer();
  label_->layer()->SetFillsBoundsOpaquely(false);
  label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label_->SetEnabledColor(text_color);
  label_->SetBackgroundColor(SK_ColorTRANSPARENT);
  label_->SetBorder(views::CreateEmptyBorder(margins));

  views::BubbleDialogDelegateView::CreateBubble(this);

  // Text box for shelf nudge should be ignored for collision detection.
  CollisionDetectionUtils::IgnoreWindowForCollisionDetection(
      GetWidget()->GetNativeWindow());
}

ContextualNudge::~ContextualNudge() = default;

void ContextualNudge::UpdateAnchorRect(const gfx::Rect& rect) {
  SetAnchorRect(rect);
}

ui::LayerType ContextualNudge::GetLayerType() const {
  return ui::LAYER_NOT_DRAWN;
}

void ContextualNudge::OnGestureEvent(ui::GestureEvent* event) {
  if (event->type() == ui::ET_GESTURE_TAP && tap_callback_) {
    event->StopPropagation();
    tap_callback_.Run();
    return;
  }

  // Pass on non tap events to the shelf (so it can handle swipe gestures that
  // start on top of the nudge). Convert event to screen coordinates, as this is
  // what Shelf::ProcessGestureEvent() expects.
  ui::GestureEvent event_in_screen(*event);
  gfx::Point location_in_screen(event->location());
  View::ConvertPointToScreen(this, &location_in_screen);
  event_in_screen.set_location(location_in_screen);

  Shelf* shelf = Shelf::ForWindow(GetWidget()->GetNativeWindow());
  if (shelf->ProcessGestureEvent(event_in_screen))
    event->StopPropagation();
  else
    views::BubbleDialogDelegateView::OnGestureEvent(event);
}

}  // namespace ash
