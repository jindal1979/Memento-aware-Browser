// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_UI_HIERARCHY_LOG_SOURCE_H_
#define CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_UI_HIERARCHY_LOG_SOURCE_H_

#include "base/macros.h"
#include "components/feedback/system_logs/system_logs_source.h"

namespace system_logs {

class UiHierarchyLogSource : public SystemLogsSource {
 public:
  UiHierarchyLogSource() : SystemLogsSource("UiHierarchy") {}
  UiHierarchyLogSource(const UiHierarchyLogSource&) = delete;
  UiHierarchyLogSource& operator=(const UiHierarchyLogSource&) = delete;
  ~UiHierarchyLogSource() override = default;

 private:
  // Overridden from SystemLogsSource:
  void Fetch(SysLogsSourceCallback callback) override;
};

}  // namespace system_logs

#endif  // CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_UI_HIERARCHY_LOG_SOURCE_H_
