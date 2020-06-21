// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia/base/inspect.h"

#include <lib/sys/inspect/cpp/component.h>

#include "components/version_info/version_info.h"

namespace cr_fuchsia {

namespace {
const char kVersion[] = "version";
const char kLastChange[] = "last_change_revision";
}  // namespace

void PublishVersionInfoToInspect(sys::ComponentInspector* inspector) {
  // These values are managed by the inspector, since they won't be updated over
  // the lifetime of the component.
  // TODO(https://crbug.com/1077428): Add release channel.
  inspector->root().CreateString(kVersion, version_info::GetVersionNumber(),
                                 inspector);
  inspector->root().CreateString(kLastChange, version_info::GetLastChange(),
                                 inspector);
}

}  // namespace cr_fuchsia
