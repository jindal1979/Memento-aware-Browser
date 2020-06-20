// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** This class represents a window. */
class WindowRootNode extends RootNodeWrapper {
  /** @override */
  onFocus() {
    super.onFocus();

    let focusNode = this.automationNode;
    while (focusNode.className !== 'BrowserFrame' &&
           focusNode.parent.role === chrome.automation.RoleType.WINDOW) {
      focusNode = focusNode.parent;
    }
    focusNode.focus();
  }

  /**
   * Creates the tree structure for a window node.
   * @param {!AutomationNode} windowNode
   * @return {!WindowRootNode}
   */
  static buildTree(windowNode) {
    const root = new WindowRootNode(windowNode);
    const childConstructor = (node) => NodeWrapper.create(node, root);

    RootNodeWrapper.findAndSetChildren(root, childConstructor);
    return root;
  }
}
