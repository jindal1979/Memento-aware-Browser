// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Class to handle navigating text. Currently, only
 * navigation and selection in editable text fields is supported.
 */
class TextNavigationManager {
  /** @private */
  constructor() {
    /** @private {number} */
    this.selectionStartIndex_ = TextNavigationManager.NO_SELECT_INDEX;

    /** @private {number} */
    this.selectionEndIndex_ = TextNavigationManager.NO_SELECT_INDEX;

    /** @private {AutomationNode} */
    this.selectionStartObject_;

    /** @private {AutomationNode} */
    this.selectionEndObject_;

    /** @private {boolean} */
    this.currentlySelecting_ = false;

    /** @private {function(chrome.automation.AutomationEvent): undefined} */
    this.selectionListener_ = this.onNavChange_.bind(this);

    /**
     * Keeps track of when there's a selection in the current node.
     * @private {boolean}
     */
    this.selectionExists_ = false;

    /**
     * Keeps track of when the clipboard is empty.
     * @private {boolean}
     */
    this.clipboardHasData_ = false;

    if (SwitchAccess.instance.improvedTextInputEnabled()) {
      chrome.clipboard.onClipboardDataChanged.addListener(
          this.updateClipboardHasData_.bind(this));
    }
  }

  static initialize() {
    TextNavigationManager.instance = new TextNavigationManager();
  }

  // =============== Static Methods ==============

  /**
   * Returns if the selection start index is set in the current node.
   * @return {boolean}
   */
  static currentlySelecting() {
    const manager = TextNavigationManager.instance;
    return (
        manager.selectionStartIndex_ !==
            TextNavigationManager.NO_SELECT_INDEX &&
        manager.currentlySelecting_);
  }

  /**
   * Jumps to the beginning of the text field (does nothing
   * if already at the beginning).
   */
  static jumpToBeginning() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(false /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.HOME, {ctrl: true});
  }

  /**
   * Jumps to the end of the text field (does nothing if
   * already at the end).
   */
  static jumpToEnd() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(false /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.END, {ctrl: true});
  }

  /**
   * Moves the text caret one character back (does nothing
   * if there are no more characters preceding the current
   * location of the caret).
   */
  static moveBackwardOneChar() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(true /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.LEFT_ARROW);
  }

  /**
   * Moves the text caret one word backwards (does nothing
   * if already at the beginning of the field). If the
   * text caret is in the middle of a word, moves the caret
   * to the beginning of that word.
   */
  static moveBackwardOneWord() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(false /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.LEFT_ARROW, {ctrl: true});
  }

  /**
   * Moves the text caret one line down (does nothing
   * if there are no lines below the current location of
   * the caret).
   */
  static moveDownOneLine() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(true /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.DOWN_ARROW);
  }

  /**
   * Moves the text caret one character forward (does nothing
   * if there are no more characters following the current
   * location of the caret).
   */
  static moveForwardOneChar() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(true /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.RIGHT_ARROW);
  }

  /**
   * Moves the text caret one word forward (does nothing if
   * already at the end of the field). If the text caret is
   * in the middle of a word, moves the caret to the end of
   * that word.
   */
  static moveForwardOneWord() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(false /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.RIGHT_ARROW, {ctrl: true});
  }

  /**
   * Moves the text caret one line up (does nothing
   * if there are no lines above the current location of
   * the caret).
   */
  static moveUpOneLine() {
    const manager = TextNavigationManager.instance;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(true /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.UP_ARROW);
  }

  /**
   * Reset the currentlySelecting variable to false, reset the selection
   * indices, and remove the listener on navigation.
   */
  static resetCurrentlySelecting() {
    const manager = TextNavigationManager.instance;
    manager.currentlySelecting_ = false;
    manager.manageNavigationListener_(false /** Removing listener */);
    manager.selectionStartIndex_ = TextNavigationManager.NO_SELECT_INDEX;
    manager.selectionEndIndex_ = TextNavigationManager.NO_SELECT_INDEX;
    if (manager.currentlySelecting_) {
      manager.setupDynamicSelection_(true /* resetCursor */);
    }
    EventHelper.simulateKeyPress(EventHelper.KeyCode.DOWN_ARROW);
  }

  /** @return {boolean} */
  static get clipboardHasData() {
    return TextNavigationManager.instance.clipboardHasData_;
  }

  /** @return {boolean} */
  static get selectionExists() {
    return TextNavigationManager.instance.selectionExists_;
  }

  /** @param {boolean} newVal */
  static set selectionExists(newVal) {
    TextNavigationManager.instance.selectionExists_ = newVal;
  }

  /**
   * Returns the selection end index.
   * @return {number}
   */
  getSelEndIndex() {
    return this.selectionEndIndex_;
  }

  /**
   * Reset the selectionStartIndex to NO_SELECT_INDEX.
   */
  resetSelStartIndex() {
    this.selectionStartIndex_ = TextNavigationManager.NO_SELECT_INDEX;
  }

  /**
   * Returns the selection start index.
   * @return {number}
   */
  getSelStartIndex() {
    return this.selectionStartIndex_;
  }

  /**
   * Sets the selection start index.
   * @param {number} startIndex
   * @param {!AutomationNode} textNode
   */
  setSelStartIndexAndNode(startIndex, textNode) {
    this.selectionStartIndex_ = startIndex;
    this.selectionStartObject_ = textNode;
  }

  /**
   * Sets the selectionStart variable based on the selection of the current
   * node. Also sets the currently selecting boolean to true.
   */
  static saveSelectStart() {
    const manager = TextNavigationManager.instance;
    chrome.automation.getFocus((focusedNode) => {
      manager.selectionStartObject_ = focusedNode;
      manager.selectionStartIndex_ = manager.getSelectionIndexFromNode_(
          manager.selectionStartObject_,
          true /* We are getting the start index.*/);
      manager.currentlySelecting_ = true;
    });
  }

  // =============== Instance Methods ==============

  /**
   * Returns either the selection start index or the selection end index of the
   * node based on the getStart param.
   * @param {!AutomationNode} node
   * @param {boolean} getStart
   * @return {number} selection start if getStart is true otherwise selection
   * end
   * @private
   */
  getSelectionIndexFromNode_(node, getStart) {
    let indexFromNode = TextNavigationManager.NO_SELECT_INDEX;
    if (getStart) {
      indexFromNode = node.textSelStart;
    } else {
      indexFromNode = node.textSelEnd;
    }
    if (indexFromNode === undefined) {
      return TextNavigationManager.NO_SELECT_INDEX;
    }
    return indexFromNode;
  }

  /**
   * Adds or removes the selection listener based on a boolean parameter.
   * @param {boolean} addListener
   * @private
   */
  manageNavigationListener_(addListener) {
    if (!this.selectionStartObject_) {
      return;
    }

    if (addListener) {
      this.selectionStartObject_.addEventListener(
          chrome.automation.EventType.TEXT_SELECTION_CHANGED,
          this.selectionListener_, false /** Don't use capture.*/);
    } else {
      this.selectionStartObject_.removeEventListener(
          chrome.automation.EventType.TEXT_SELECTION_CHANGED,
          this.selectionListener_, false /** Don't use capture.*/);
    }
  }

  /**
   * Function to handle changes in the cursor position during selection.
   * This function will remove the selection listener and set the end of the
   * selection based on the new position.
   * @private
   */
  onNavChange_() {
    this.manageNavigationListener_(false);
    if (this.currentlySelecting_) {
      TextNavigationManager.saveSelectEnd();
    }
  }

  /**
   * Sets the selectionEnd variable based on the selection of the current node.
   */
  static saveSelectEnd() {
    const manager = TextNavigationManager.instance;
    chrome.automation.getFocus((focusedNode) => {
      manager.selectionEndObject_ = focusedNode;
      manager.selectionEndIndex_ = manager.getSelectionIndexFromNode_(
          manager.selectionEndObject_,
          false /*We are not getting the start index.*/);
      manager.saveSelection_();
    });
  }

  /**
   * Sets the selection using the selectionStart and selectionEnd
   * as the offset input for setDocumentSelection and the parameter
   * textNode as the object input for setDocumentSelection.
   * @private
   */
  saveSelection_() {
    if (this.selectionStartIndex_ == TextNavigationManager.NO_SELECT_INDEX ||
        this.selectionEndIndex_ == TextNavigationManager.NO_SELECT_INDEX) {
      console.log(
          'Selection bounds are not set properly:', this.selectionStartIndex_,
          this.selectionEndIndex_);
    } else {
      chrome.automation.setDocumentSelection({
        anchorObject: this.selectionStartObject_,
        anchorOffset: this.selectionStartIndex_,
        focusObject: this.selectionEndObject_,
        focusOffset: this.selectionEndIndex_
      });
    }
  }

  /**
   * Sets up the cursor position and selection listener for dynamic selection.
   * If the needToResetCursor boolean is true, the function will move the cursor
   * to the end point of the selection before adding the event listener. If not,
   * it will simply add the listener.
   * @param {boolean} needToResetCursor
   * @private
   */
  setupDynamicSelection_(needToResetCursor) {
    /**
     * TODO(crbug.com/999400): Work on text selection dynamic highlight and
     * text selection implementation.
     */
    if (needToResetCursor) {
      if (TextNavigationManager.currentlySelecting() &&
          this.selectionEndIndex_ != TextNavigationManager.NO_SELECT_INDEX) {
        // Move the cursor to the end of the existing selection.
        chrome.automation.setDocumentSelection({
          anchorObject: this.selectionEndObject_,
          anchorOffset: this.selectionEndIndex_,
          focusObject: this.selectionEndObject_,
          focusOffset: this.selectionEndIndex_
        });
      }
    }
    this.manageNavigationListener_(true /** Add the listener */);
  }

  /*
   * TODO(rosalindag): Add functionality to catch when clipboardHasData_ needs
   * to be set to false.
   * Set the clipboardHasData variable to true and reload the menu.
   * @private
   */
  updateClipboardHasData_() {
    this.clipboardHasData_ = true;
    const node = NavigationManager.currentNode;
    if (node.hasAction(SwitchAccessMenuAction.PASTE)) {
      MenuManager.reloadActionsForNode(node);
    }
  }
}

// Constant to indicate selection index is not set.
TextNavigationManager.NO_SELECT_INDEX = -1;
