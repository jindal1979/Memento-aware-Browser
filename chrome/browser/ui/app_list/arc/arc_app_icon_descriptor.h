// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_ARC_ARC_APP_ICON_DESCRIPTOR_H_
#define CHROME_BROWSER_UI_APP_LIST_ARC_ARC_APP_ICON_DESCRIPTOR_H_

#include <string>

#include "ui/base/resource/scale_factor.h"

struct ArcAppIconDescriptor {
  ArcAppIconDescriptor(int dip_size, ui::ScaleFactor scale_factor);

  // Returns raw icon size in pixels.
  int GetSizeInPixels() const;
  // Used as a file name to store icon on the disk.
  std::string GetName() const;

  bool operator==(const ArcAppIconDescriptor& other) const;
  bool operator!=(const ArcAppIconDescriptor& other) const;
  bool operator<(const ArcAppIconDescriptor& other) const;

  int dip_size;
  ui::ScaleFactor scale_factor;
};

#endif  // CHROME_BROWSER_UI_APP_LIST_ARC_ARC_APP_ICON_DESCRIPTOR_H_
