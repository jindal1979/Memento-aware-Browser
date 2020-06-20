// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/unified/unified_system_tray_view.h"

#include <numeric>

#include "ash/public/cpp/ash_features.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shelf/shelf.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shell.h"
#include "ash/style/ash_color_provider.h"
#include "ash/style/default_color_constants.h"
#include "ash/system/message_center/ash_message_center_lock_screen_controller.h"
#include "ash/system/message_center/unified_message_center_view.h"
#include "ash/system/tray/interacted_by_tap_recorder.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/unified/detailed_view_controller.h"
#include "ash/system/unified/feature_pod_button.h"
#include "ash/system/unified/feature_pods_container_view.h"
#include "ash/system/unified/notification_hidden_view.h"
#include "ash/system/unified/page_indicator_view.h"
#include "ash/system/unified/top_shortcuts_view.h"
#include "ash/system/unified/unified_managed_device_view.h"
#include "ash/system/unified/unified_system_info_view.h"
#include "ash/system/unified/unified_system_tray_controller.h"
#include "ash/system/unified/unified_system_tray_model.h"
#include "ash/wm/tablet_mode/tablet_mode_controller.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/message_center_constants.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/focus/focus_search.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/painter.h"

namespace ash {

namespace {

class DetailedViewContainer : public views::View {
 public:
  DetailedViewContainer() = default;

  ~DetailedViewContainer() override = default;

  // views::View:
  void Layout() override {
    for (auto* child : children())
      child->SetBoundsRect(GetContentsBounds());
    views::View::Layout();
  }

  const char* GetClassName() const override { return "DetailedViewContainer"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DetailedViewContainer);
};

class AccessibilityFocusHelperView : public views::View {
 public:
  AccessibilityFocusHelperView(UnifiedSystemTrayController* controller)
      : controller_(controller) {}

  bool HandleAccessibleAction(const ui::AXActionData& action_data) override {
    GetFocusManager()->ClearFocus();
    GetFocusManager()->SetStoredFocusView(nullptr);
    controller_->FocusOut(false);
    return true;
  }

  // views::View:
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override {
    node_data->role = ax::mojom::Role::kListItem;
  }

 private:
  UnifiedSystemTrayController* controller_;
};

}  // namespace

UnifiedSlidersContainerView::UnifiedSlidersContainerView(
    bool initially_expanded)
    : expanded_amount_(initially_expanded ? 1.0 : 0.0) {
  SetVisible(initially_expanded);
}

UnifiedSlidersContainerView::~UnifiedSlidersContainerView() = default;

void UnifiedSlidersContainerView::SetExpandedAmount(double expanded_amount) {
  DCHECK(0.0 <= expanded_amount && expanded_amount <= 1.0);
  SetVisible(expanded_amount > 0.0);
  expanded_amount_ = expanded_amount;
  InvalidateLayout();
  UpdateOpacity();
}

int UnifiedSlidersContainerView::GetExpandedHeight() const {
  return std::accumulate(children().cbegin(), children().cend(), 0,
                         [](int height, const auto* v) {
                           return height + v->GetHeightForWidth(kTrayMenuWidth);
                         });
}

void UnifiedSlidersContainerView::Layout() {
  int y = 0;
  for (auto* child : children()) {
    int height = child->GetHeightForWidth(kTrayMenuWidth);
    child->SetBounds(0, y, kTrayMenuWidth, height);
    y += height;
  }
}

gfx::Size UnifiedSlidersContainerView::CalculatePreferredSize() const {
  return gfx::Size(kTrayMenuWidth, GetExpandedHeight() * expanded_amount_);
}

const char* UnifiedSlidersContainerView::GetClassName() const {
  return "UnifiedSlidersContainerView";
}

void UnifiedSlidersContainerView::UpdateOpacity() {
  const int height = GetPreferredSize().height();
  for (auto* child : children()) {
    double opacity = 1.0;
    if (child->y() > height) {
      opacity = 0.0;
    } else if (child->bounds().bottom() < height) {
      opacity = 1.0;
    } else {
      const double ratio =
          static_cast<double>(height - child->y()) / child->height();
      // TODO(tetsui): Confirm the animation curve with UX.
      opacity = std::max(0., 2. * ratio - 1.);
    }
    child->layer()->SetOpacity(opacity);
  }
}

// The container view for the system tray, i.e. the panel containing settings
// buttons and sliders (e.g. sign out, lock, volume slider, etc.).
class UnifiedSystemTrayView::SystemTrayContainer : public views::View {
 public:
  SystemTrayContainer()
      : layout_manager_(SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical))) {}
  SystemTrayContainer(const SystemTrayContainer&) = delete;
  SystemTrayContainer& operator=(const SystemTrayContainer&) = delete;

  ~SystemTrayContainer() override = default;

  void SetFlexForView(views::View* view) {
    DCHECK_EQ(view->parent(), this);
    layout_manager_->SetFlexForView(view, 1);
  }

  // views::View:
  const char* GetClassName() const override { return "SystemTrayContainer"; }

 private:
  views::BoxLayout* const layout_manager_;
};

// static
SkColor UnifiedSystemTrayView::GetBackgroundColor() {
  auto background_type = Shelf::ForWindow(Shell::GetPrimaryRootWindow())
                             ->shelf_widget()
                             ->GetBackgroundType();
  AshColorProvider::BaseLayerType layer_type =
      (background_type == ShelfBackgroundType::kMaximized ||
       background_type == ShelfBackgroundType::kInApp) ||
       !features::IsBackgroundBlurEnabled()
          ? AshColorProvider::BaseLayerType::kTransparent90
          : AshColorProvider::BaseLayerType::kTransparent80;

  SkColor background_color = AshColorProvider::Get()->GetBaseLayerColor(
      layer_type, AshColorProvider::AshColorMode::kDark);

  return ShelfConfig::Get()->GetThemedColorFromWallpaper(background_color);
}

// static
SkColor UnifiedSystemTrayView::GetFocusRingColor() {
  return ShelfConfig::Get()->shelf_focus_border_color();
}

// static
std::unique_ptr<views::Background> UnifiedSystemTrayView::CreateBackground() {
  return views::CreateBackgroundFromPainter(
      views::Painter::CreateSolidRoundRectPainter(GetBackgroundColor(),
                                                  kUnifiedTrayCornerRadius));
}

UnifiedSystemTrayView::UnifiedSystemTrayView(
    UnifiedSystemTrayController* controller,
    bool initially_expanded)
    : expanded_amount_(initially_expanded ? 1.0 : 0.0),
      controller_(controller),
      notification_hidden_view_(new NotificationHiddenView()),
      top_shortcuts_view_(new TopShortcutsView(controller_)),
      feature_pods_container_(
          new FeaturePodsContainerView(controller_, initially_expanded)),
      page_indicator_view_(
          new PageIndicatorView(controller_, initially_expanded)),
      sliders_container_(new UnifiedSlidersContainerView(initially_expanded)),
      system_info_view_(new UnifiedSystemInfoView(controller_)),
      system_tray_container_(new SystemTrayContainer()),
      detailed_view_container_(new DetailedViewContainer()),
      focus_search_(std::make_unique<views::FocusSearch>(this, false, false)),
      interacted_by_tap_recorder_(
          std::make_unique<InteractedByTapRecorder>(this)) {
  DCHECK(controller_);

  SetLayoutManager(std::make_unique<views::FillLayout>());
  auto add_layered_child = [](views::View* parent, views::View* child) {
    parent->AddChildView(child);
    child->SetPaintToLayer();
    child->layer()->SetFillsBoundsOpaquely(false);
  };

  SessionControllerImpl* session_controller =
      Shell::Get()->session_controller();

  notification_hidden_view_->SetVisible(
      session_controller->GetUserSession(0) &&
      session_controller->IsScreenLocked() &&
      !AshMessageCenterLockScreenController::IsEnabled());
  add_layered_child(system_tray_container_, notification_hidden_view_);

  AddChildView(system_tray_container_);

  add_layered_child(system_tray_container_, top_shortcuts_view_);
  system_tray_container_->AddChildView(feature_pods_container_);
  system_tray_container_->AddChildView(page_indicator_view_);
  system_tray_container_->AddChildView(sliders_container_);
  add_layered_child(system_tray_container_, system_info_view_);

  system_tray_container_->SetFlexForView(page_indicator_view_);

  if (features::IsManagedDeviceUIRedesignEnabled()) {
    managed_device_view_ = new UnifiedManagedDeviceView();
    system_tray_container_->AddChildView(managed_device_view_);
  }

  detailed_view_container_->SetVisible(false);
  add_layered_child(this, detailed_view_container_);

  top_shortcuts_view_->SetExpandedAmount(expanded_amount_);

  system_tray_container_->AddChildView(
      new AccessibilityFocusHelperView(controller_));
}

UnifiedSystemTrayView::~UnifiedSystemTrayView() = default;

void UnifiedSystemTrayView::SetMaxHeight(int max_height) {
  max_height_ = max_height;

  // FeaturePodsContainer can adjust it's height by reducing the number of rows
  // it uses. It will calculate how many rows to use based on the max height
  // passed here.
  feature_pods_container_->SetMaxHeight(
      max_height - top_shortcuts_view_->GetPreferredSize().height() -
      page_indicator_view_->GetPreferredSize().height() -
      sliders_container_->GetExpandedHeight() -
      system_info_view_->GetPreferredSize().height());
}

void UnifiedSystemTrayView::AddFeaturePodButton(FeaturePodButton* button) {
  feature_pods_container_->AddFeaturePodButton(button);
}

void UnifiedSystemTrayView::AddSliderView(views::View* slider_view) {
  slider_view->SetPaintToLayer();
  slider_view->layer()->SetFillsBoundsOpaquely(false);
  sliders_container_->AddChildView(slider_view);
}

void UnifiedSystemTrayView::SetDetailedView(views::View* detailed_view) {
  auto system_tray_size = system_tray_container_->GetPreferredSize();
  system_tray_container_->SetVisible(false);

  detailed_view_container_->RemoveAllChildViews(true /* delete_children */);
  detailed_view_container_->AddChildView(detailed_view);
  detailed_view_container_->SetVisible(true);
  detailed_view_container_->SetPreferredSize(system_tray_size);
  detailed_view->InvalidateLayout();
  Layout();
}

void UnifiedSystemTrayView::ResetDetailedView() {
  detailed_view_container_->RemoveAllChildViews(true /* delete_children */);
  detailed_view_container_->SetVisible(false);
  system_tray_container_->SetVisible(true);
  sliders_container_->UpdateOpacity();
  PreferredSizeChanged();
  Layout();
}

void UnifiedSystemTrayView::SaveFocus() {
  auto* focus_manager = GetFocusManager();
  if (!focus_manager)
    return;

  saved_focused_view_ = focus_manager->GetFocusedView();
}

void UnifiedSystemTrayView::RestoreFocus() {
  if (saved_focused_view_)
    saved_focused_view_->RequestFocus();
}

void UnifiedSystemTrayView::SetExpandedAmount(double expanded_amount) {
  DCHECK(0.0 <= expanded_amount && expanded_amount <= 1.0);
  expanded_amount_ = expanded_amount;

  top_shortcuts_view_->SetExpandedAmount(expanded_amount);
  feature_pods_container_->SetExpandedAmount(expanded_amount);
  page_indicator_view_->SetExpandedAmount(expanded_amount);
  sliders_container_->SetExpandedAmount(expanded_amount);

  if (!IsTransformEnabled()) {
    PreferredSizeChanged();
    // It is possible that the ratio between |message_center_view_| and others
    // can change while the bubble size remain unchanged.
    Layout();
    return;
  }

  // Note: currently transforms are only enabled when there are no
  // notifications, so we can consider only the system tray height.
  if (height() != GetExpandedSystemTrayHeight())
    PreferredSizeChanged();
  Layout();
}

int UnifiedSystemTrayView::GetExpandedSystemTrayHeight() const {
  return (notification_hidden_view_->GetVisible()
              ? notification_hidden_view_->GetPreferredSize().height()
              : 0) +
         top_shortcuts_view_->GetPreferredSize().height() +
         feature_pods_container_->GetExpandedHeight() +
         page_indicator_view_->GetExpandedHeight() +
         sliders_container_->GetExpandedHeight() +
         system_info_view_->GetPreferredSize().height();
}

int UnifiedSystemTrayView::GetCollapsedSystemTrayHeight() const {
  return (notification_hidden_view_->GetVisible()
              ? notification_hidden_view_->GetPreferredSize().height()
              : 0) +
         top_shortcuts_view_->GetPreferredSize().height() +
         feature_pods_container_->GetCollapsedHeight() +
         system_info_view_->GetPreferredSize().height();
}

int UnifiedSystemTrayView::GetCurrentHeight() const {
  return GetPreferredSize().height();
}

bool UnifiedSystemTrayView::IsTransformEnabled() const {
  // TODO(amehfooz): Remove transform code completely, the code does not work
  // and isn't needed after Oshima's performance improvement changes for the
  // tray.
  return false;
}


int UnifiedSystemTrayView::GetVisibleFeaturePodCount() const {
  return feature_pods_container_->GetVisibleCount();
}

base::string16 UnifiedSystemTrayView::GetDetailedViewAccessibleName() const {
  return controller_->detailed_view_controller()->GetAccessibleName();
}

bool UnifiedSystemTrayView::IsDetailedViewShown() const {
  return detailed_view_container_->GetVisible();
}

views::View* UnifiedSystemTrayView::GetFirstFocusableChild() {
  FocusTraversable* focus_traversable = GetFocusTraversable();
  views::View* focus_traversable_view = this;
  return focus_search_->FindNextFocusableView(
      nullptr, views::FocusSearch::SearchDirection::kForwards,
      views::FocusSearch::TraversalDirection::kDown,
      views::FocusSearch::StartingViewPolicy::kSkipStartingView,
      views::FocusSearch::AnchoredDialogPolicy::kCanGoIntoAnchoredDialog,
      &focus_traversable, &focus_traversable_view);
}

views::View* UnifiedSystemTrayView::GetLastFocusableChild() {
  FocusTraversable* focus_traversable = GetFocusTraversable();
  views::View* focus_traversable_view = this;
  return focus_search_->FindNextFocusableView(
      nullptr, views::FocusSearch::SearchDirection::kBackwards,
      views::FocusSearch::TraversalDirection::kDown,
      views::FocusSearch::StartingViewPolicy::kSkipStartingView,
      views::FocusSearch::AnchoredDialogPolicy::kCanGoIntoAnchoredDialog,
      &focus_traversable, &focus_traversable_view);
}

void UnifiedSystemTrayView::FocusEntered(bool reverse) {
  views::View* focus_view =
      reverse ? GetLastFocusableChild() : GetFirstFocusableChild();
  GetFocusManager()->ClearFocus();
  GetFocusManager()->SetFocusedView(focus_view);
}

gfx::Size UnifiedSystemTrayView::CalculatePreferredSize() const {
  int expanded_height = GetExpandedSystemTrayHeight();
  int collapsed_height = GetCollapsedSystemTrayHeight();

  return gfx::Size(kTrayMenuWidth,
                   collapsed_height + ((expanded_height - collapsed_height) *
                                       expanded_amount_));
}

void UnifiedSystemTrayView::OnGestureEvent(ui::GestureEvent* event) {
  gfx::PointF screen_location = event->root_location_f();
  switch (event->type()) {
    case ui::ET_GESTURE_SCROLL_BEGIN:
      controller_->BeginDrag(screen_location);
      event->SetHandled();
      break;
    case ui::ET_GESTURE_SCROLL_UPDATE:
      controller_->UpdateDrag(screen_location);
      event->SetHandled();
      break;
    case ui::ET_GESTURE_END:
      controller_->EndDrag(screen_location);
      event->SetHandled();
      break;
    case ui::ET_SCROLL_FLING_START:
      controller_->Fling(event->details().velocity_y());
      break;
    default:
      break;
  }
}

void UnifiedSystemTrayView::ChildPreferredSizeChanged(views::View* child) {
  // The size change is not caused by SetExpandedAmount(), because they don't
  // trigger PreferredSizeChanged().
  PreferredSizeChanged();
}

const char* UnifiedSystemTrayView::GetClassName() const {
  return "UnifiedSystemTrayView";
}

void UnifiedSystemTrayView::AddedToWidget() {
  focus_manager_ = GetFocusManager();
  if (focus_manager_)
    focus_manager_->AddFocusChangeListener(this);
}

void UnifiedSystemTrayView::RemovedFromWidget() {
  if (!focus_manager_)
    return;
  focus_manager_->RemoveFocusChangeListener(this);
  focus_manager_ = nullptr;
}

views::FocusTraversable* UnifiedSystemTrayView::GetFocusTraversable() {
  return this;
}

views::FocusSearch* UnifiedSystemTrayView::GetFocusSearch() {
  return focus_search_.get();
}

views::FocusTraversable* UnifiedSystemTrayView::GetFocusTraversableParent() {
  return nullptr;
}

views::View* UnifiedSystemTrayView::GetFocusTraversableParentView() {
  return this;
}

void UnifiedSystemTrayView::OnWillChangeFocus(views::View* before,
                                              views::View* now) {}

void UnifiedSystemTrayView::OnDidChangeFocus(views::View* before,
                                             views::View* now) {
  if (feature_pods_container_->Contains(now)) {
    feature_pods_container_->EnsurePageWithButton(now);
  }

  views::View* first_view = GetFirstFocusableChild();
  views::View* last_view = GetLastFocusableChild();

  bool focused_out = false;
  if (before == last_view && now == first_view)
    focused_out = controller_->FocusOut(false);
  else if (before == first_view && now == last_view)
    focused_out = controller_->FocusOut(true);

  if (focused_out) {
    GetFocusManager()->ClearFocus();
    GetFocusManager()->SetStoredFocusView(nullptr);
  }
}

}  // namespace ash
