// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * This class handles interactions with editable text fields.
 */
class EditableTextNode extends NodeWrapper {
  /**
   * @param {!AutomationNode} baseNode
   * @param {?SARootNode} parent
   */
  constructor(baseNode, parent) {
    super(baseNode, parent);
  }

  // ================= Getters and setters =================

  /** @override */
  get actions() {
    const actions = super.actions;
    // The SELECT action is used to press buttons, etc. For text inputs, the
    // equivalent action is KEYBOARD, which focuses the input and opens the
    // keyboard.
    const selectIndex = actions.indexOf(SwitchAccessMenuAction.SELECT);
    if (selectIndex >= 0) {
      actions.splice(selectIndex, 1);
    }

    actions.push(SwitchAccessMenuAction.KEYBOARD);
    actions.push(SwitchAccessMenuAction.DICTATION);

    if (SwitchAccess.instance.improvedTextInputEnabled()) {
      actions.push(SwitchAccessMenuAction.MOVE_CURSOR);
      actions.push(SwitchAccessMenuAction.JUMP_TO_BEGINNING_OF_TEXT);
      actions.push(SwitchAccessMenuAction.JUMP_TO_END_OF_TEXT);
      actions.push(SwitchAccessMenuAction.MOVE_BACKWARD_ONE_CHAR_OF_TEXT);
      actions.push(SwitchAccessMenuAction.MOVE_FORWARD_ONE_CHAR_OF_TEXT);
      actions.push(SwitchAccessMenuAction.MOVE_BACKWARD_ONE_WORD_OF_TEXT);
      actions.push(SwitchAccessMenuAction.MOVE_FORWARD_ONE_WORD_OF_TEXT);
      actions.push(SwitchAccessMenuAction.MOVE_DOWN_ONE_LINE_OF_TEXT);
      actions.push(SwitchAccessMenuAction.MOVE_UP_ONE_LINE_OF_TEXT);

      actions.push(SwitchAccessMenuAction.START_TEXT_SELECTION);
      if (TextNavigationManager.currentlySelecting()) {
        actions.push(SwitchAccessMenuAction.END_TEXT_SELECTION);
      }

      if (TextNavigationManager.selectionExists) {
        actions.push(SwitchAccessMenuAction.CUT);
        actions.push(SwitchAccessMenuAction.COPY);
      }
      if (TextNavigationManager.clipboardHasData) {
        actions.push(SwitchAccessMenuAction.PASTE);
      }
    }
    return actions;
  }

  // ================= General methods =================

  /** @override */
  doDefaultAction() {
    this.performAction(SwitchAccessMenuAction.KEYBOARD);
  }

  /** @override */
  performAction(action) {
    switch (action) {
      case SwitchAccessMenuAction.KEYBOARD:
        NavigationManager.enterKeyboard();
        return SAConstants.ActionResponse.CLOSE_MENU;
      case SwitchAccessMenuAction.DICTATION:
        chrome.accessibilityPrivate.toggleDictation();
        return SAConstants.ActionResponse.CLOSE_MENU;
      case SwitchAccessMenuAction.MOVE_CURSOR:
        return SAConstants.ActionResponse.OPEN_TEXT_NAVIGATION_MENU;

      case SwitchAccessMenuAction.CUT:
        EventHelper.simulateKeyPress(EventHelper.KeyCode.X, {ctrl: true});
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.COPY:
        EventHelper.simulateKeyPress(EventHelper.KeyCode.C, {ctrl: true});
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.PASTE:
        EventHelper.simulateKeyPress(EventHelper.KeyCode.V, {ctrl: true});
        return SAConstants.ActionResponse.REMAIN_OPEN;

      case SwitchAccessMenuAction.START_TEXT_SELECTION:
        TextNavigationManager.saveSelectStart();
        return SAConstants.ActionResponse.OPEN_TEXT_NAVIGATION_MENU;
      case SwitchAccessMenuAction.END_TEXT_SELECTION:
        TextNavigationManager.saveSelectEnd();
        return SAConstants.ActionResponse.RELOAD_MAIN_MENU;

      case SwitchAccessMenuAction.JUMP_TO_BEGINNING_OF_TEXT:
        TextNavigationManager.jumpToBeginning();
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.JUMP_TO_END_OF_TEXT:
        TextNavigationManager.jumpToEnd();
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.MOVE_BACKWARD_ONE_CHAR_OF_TEXT:
        TextNavigationManager.moveBackwardOneChar();
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.MOVE_BACKWARD_ONE_WORD_OF_TEXT:
        TextNavigationManager.moveBackwardOneWord();
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.MOVE_DOWN_ONE_LINE_OF_TEXT:
        TextNavigationManager.moveDownOneLine();
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.MOVE_FORWARD_ONE_CHAR_OF_TEXT:
        TextNavigationManager.moveForwardOneChar();
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.MOVE_UP_ONE_LINE_OF_TEXT:
        TextNavigationManager.moveUpOneLine();
        return SAConstants.ActionResponse.REMAIN_OPEN;
      case SwitchAccessMenuAction.MOVE_FORWARD_ONE_WORD_OF_TEXT:
        TextNavigationManager.moveForwardOneWord();
        return SAConstants.ActionResponse.REMAIN_OPEN;
    }
    return super.performAction(action);
  }
}
