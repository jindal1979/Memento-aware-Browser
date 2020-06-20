// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** This class handles interactions with sliders. */
class SliderNode extends NodeWrapper {
  /**
   * @param {!AutomationNode} baseNode
   * @param {?SARootNode} parent
   */
  constructor(baseNode, parent) {
    super(baseNode, parent);
    this.isCustomSlider_ = !!baseNode.htmlAttributes.role;
  }

  /** @override */
  onFocus() {
    super.onFocus();
    this.automationNode.focus();
  }

  /** @override */
  performAction(action) {
    // Currently, custom sliders have no way to support increment/decrement via
    // the automation API. We handle this case by simulating left/right arrow
    // presses.
    if (this.isCustomSlider_) {
      if (action === SwitchAccessMenuAction.INCREMENT) {
        EventHelper.simulateKeyPress(EventHelper.KeyCode.RIGHT_ARROW);
        return SAConstants.ActionResponse.REMAIN_OPEN;
      } else if (action === SwitchAccessMenuAction.DECREMENT) {
        EventHelper.simulateKeyPress(EventHelper.KeyCode.LEFT_ARROW);
        return SAConstants.ActionResponse.REMAIN_OPEN;
      }
    }

    return super.performAction(action);
  }
}
