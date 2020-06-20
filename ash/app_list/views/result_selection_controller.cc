// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/app_list/views/result_selection_controller.h"

#include "ash/app_list/app_list_util.h"
#include "ash/app_list/views/search_result_container_view.h"

namespace ash {

ResultLocationDetails::ResultLocationDetails() = default;

ResultLocationDetails::ResultLocationDetails(int container_index,
                                             int container_count,
                                             int result_index,
                                             int result_count,
                                             bool container_is_horizontal)
    : container_index(container_index),
      container_count(container_count),
      result_index(result_index),
      result_count(result_count),
      container_is_horizontal(container_is_horizontal) {}

bool ResultLocationDetails::operator==(
    const ResultLocationDetails& other) const {
  return container_index == other.container_index &&
         container_count == other.container_count &&
         result_index == other.result_index &&
         result_count == other.result_count &&
         container_is_horizontal == other.container_is_horizontal;
}

bool ResultLocationDetails::operator!=(
    const ResultLocationDetails& other) const {
  return !(*this == other);
}

ResultSelectionController::ResultSelectionController(
    const ResultSelectionModel* result_container_views,
    const base::RepeatingClosure& selection_change_callback)
    : result_selection_model_(result_container_views),
      selection_change_callback_(selection_change_callback) {}

ResultSelectionController::~ResultSelectionController() = default;

ResultSelectionController::MoveResult ResultSelectionController::MoveSelection(
    const ui::KeyEvent& event) {
  if (block_selection_changes_)
    return MoveResult::kNone;

  ResultLocationDetails next_location;
  if (!selected_location_details_) {
    ResetSelection(&event, false /* default_selection */);
    return MoveResult::kResultChanged;
  }

  MoveResult result = GetNextResultLocation(event, &next_location);
  if (result == MoveResult::kResultChanged)
    SetSelection(next_location, event.IsShiftDown());
  return result;
}

void ResultSelectionController::ResetSelection(const ui::KeyEvent* key_event,
                                               bool default_selection) {
  // Prevents crash on start up
  if (result_selection_model_->size() == 0)
    return;

  if (block_selection_changes_)
    return;

  selected_location_details_ = std::make_unique<ResultLocationDetails>(
      0 /* container_index */,
      result_selection_model_->size() /* container_count */,
      0 /* result_index */,
      result_selection_model_->at(0)->num_results() /* result_count */,
      result_selection_model_->at(0)
          ->horizontally_traversable() /* container_is_horizontal */);

  const bool is_up_key = key_event && key_event->key_code() == ui::VKEY_UP;
  const bool is_shift_tab = key_event &&
                            key_event->key_code() == ui::VKEY_TAB &&
                            key_event->IsShiftDown();
  // Note: left and right arrows are used primarily for traversal in horizontal
  // containers, so treat "back" arrow as other non-traversal keys when deciding
  // whether to reverse selection direction.
  if (is_up_key || is_shift_tab)
    ChangeContainer(selected_location_details_.get(), -1);

  auto* new_selection =
      result_selection_model_->at(selected_location_details_->container_index)
          ->GetResultViewAt(selected_location_details_->result_index);

  if (new_selection && new_selection->selected())
    return;

  if (selected_result_)
    selected_result_->SetSelected(false, base::nullopt);

  selected_result_ = new_selection;

  // Set the state of the new selected result.
  if (selected_result_) {
    selected_result_->SetSelected(true, is_shift_tab);
    selected_result_->set_is_default_result(default_selection);
  }

  selection_change_callback_.Run();
}

void ResultSelectionController::ClearSelection() {
  selected_location_details_ = nullptr;
  if (selected_result_) {
    // Reset the state of the previous selected result.
    selected_result_->SetSelected(false, base::nullopt);
    selected_result_->set_is_default_result(false);
  }
  selected_result_ = nullptr;
}

ResultSelectionController::MoveResult
ResultSelectionController::GetNextResultLocation(
    const ui::KeyEvent& event,
    ResultLocationDetails* next_location) {
  return GetNextResultLocationForLocation(event, *selected_location_details_,
                                          next_location);
}

ResultSelectionController::MoveResult
ResultSelectionController::GetNextResultLocationForLocation(
    const ui::KeyEvent& event,
    const ResultLocationDetails& location,
    ResultLocationDetails* next_location) {
  *next_location = location;

  // Only arrow keys (unhandled and unmodified) or the tab key will change our
  // selection.
  if (!(IsUnhandledArrowKeyEvent(event) || event.key_code() == ui::VKEY_TAB))
    return MoveResult::kNone;

  if (selected_result_ && event.key_code() == ui::VKEY_TAB &&
      selected_result_->SelectNextResultAction(event.IsShiftDown())) {
    selection_change_callback_.Run();
    return MoveResult::kNone;
  }

  switch (event.key_code()) {
    case ui::VKEY_TAB:
      if (event.IsShiftDown()) {
        // Reverse tab traversal always goes to the 'previous' result.
        if (location.is_first_result()) {
          ChangeContainer(next_location, location.container_index - 1);

          if (next_location->container_index >= location.container_index)
            return MoveResult::kSelectionCycleRejected;

        } else {
          --next_location->result_index;
        }
      } else {
        // Forward tab traversal always goes to the 'next' result.
        if (location.is_last_result()) {
          ChangeContainer(next_location, location.container_index + 1);

          if (next_location->container_index <= location.container_index)
            return MoveResult::kSelectionCycleRejected;
        } else {
          ++next_location->result_index;
        }
      }

      break;
    case ui::VKEY_UP:
      if (location.container_is_horizontal || location.is_first_result()) {
        // Traversing 'up' from the top of a container changes containers.
        ChangeContainer(next_location, location.container_index - 1);

        if (next_location->container_index >= location.container_index)
          return MoveResult::kSelectionCycleRejected;
      } else {
        // Traversing 'up' moves up one result.
        --next_location->result_index;
      }
      break;
    case ui::VKEY_DOWN:
      if (location.container_is_horizontal || location.is_last_result()) {
        // Traversing 'down' from the bottom of a container changes containers.
        ChangeContainer(next_location, location.container_index + 1);
        if (next_location->container_index <= location.container_index)
          return MoveResult::kSelectionCycleRejected;
      } else {
        // Traversing 'down' moves down one result.
        ++next_location->result_index;
      }
      break;
    case ui::VKEY_RIGHT:
    case ui::VKEY_LEFT: {
      // Containers are stacked, so left/right does not traverse vertical
      // containers.
      if (!location.container_is_horizontal)
        break;

      ui::KeyboardCode forward = ui::VKEY_RIGHT;

      // If RTL is active, 'forward' is left instead.
      if (base::i18n::IsRTL())
        forward = ui::VKEY_LEFT;

      // Traversing should move one result, but loop within the
      // container.
      if (event.key_code() == forward) {
        // If not at the last result, increment forward.
        if (location.result_index != location.result_count - 1)
          ++next_location->result_index;
        else
          // Loop back to the first result.
          next_location->result_index = 0;
      } else {
        // If not at the first result, increment backward.
        if (location.result_index != 0)
          --next_location->result_index;
        else
          // Loop around to the last result.
          next_location->result_index = location.result_count - 1;
      }
    } break;

    default:
      NOTREACHED();
  }
  return *next_location == location ? MoveResult::kNone
                                    : MoveResult::kResultChanged;
}

void ResultSelectionController::SetSelection(
    const ResultLocationDetails& location,
    bool reverse_tab_order) {
  ClearSelection();

  selected_result_ = GetResultAtLocation(location);
  // SetSelection is only called by MoveSelection when user changes
  // selected result, therefore, the result selected by user is not
  // a default result.
  selected_result_->set_is_default_result(false);
  selected_location_details_ =
      std::make_unique<ResultLocationDetails>(location);
  selected_result_->SetSelected(true, reverse_tab_order);
  selection_change_callback_.Run();
}

SearchResultBaseView* ResultSelectionController::GetResultAtLocation(
    const ResultLocationDetails& location) {
  SearchResultContainerView* located_container =
      result_selection_model_->at(location.container_index);
  return located_container->GetResultViewAt(location.result_index);
}

void ResultSelectionController::ChangeContainer(
    ResultLocationDetails* location_details,
    int new_container_index) {
  if (new_container_index == location_details->container_index) {
    return;
  }

  // If the index is advancing
  bool container_advancing =
      new_container_index > location_details->container_index;

  // This handles 'looping', so if the selection goes off the end of the
  // container, it will come back to the beginning.
  int new_container = new_container_index;
  if (new_container < 0) {
    new_container = location_details->container_count - 1;
  }
  if (new_container >= location_details->container_count)
    new_container = 0;

  // Because all containers always exist, we need to make sure there are results
  // in the next container.
  while (result_selection_model_->at(new_container)->num_results() <= 0) {
    if (container_advancing) {
      ++new_container;
    } else {
      --new_container;
    }

    // Prevent any potential infinite looping by resetting to '0', a container
    // that should never be empty.
    if (new_container <= 0 ||
        new_container >= location_details->container_count) {
      new_container = 0;
      break;
    }
  }

  // Updates |result_count| and |container_is_horizontal| based on
  // |new_container|.
  location_details->result_count =
      result_selection_model_->at(new_container)->num_results();
  location_details->container_is_horizontal =
      result_selection_model_->at(new_container)->horizontally_traversable();

  // Updates |result_index| to the first or the last result in the container
  // based on whether the |container_index| is increasing or decreasing.
  if (container_advancing) {
    location_details->result_index = 0;
  } else {
    location_details->result_index = location_details->result_count - 1;
  }

  // Finally, we update |container_index| to the new index. |container_count|
  // doesn't change in this function.
  location_details->container_index = new_container;
}

}  // namespace ash
