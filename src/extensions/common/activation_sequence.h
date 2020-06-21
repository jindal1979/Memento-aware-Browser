// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_ACTIVATION_SEQUENCE_H_
#define EXTENSIONS_COMMON_ACTIVATION_SEQUENCE_H_

#include "base/util/type_safety/strong_alias.h"

namespace extensions {

// Unique identifier for an extension's activation->deactivation span.
//
// Applies to Service Worker based extensions. This is used to detect if a
// PendingTask for an extension expired at the time of executing the task, due
// to extension activation after deactivation.
using ActivationSequence =
    ::util::StrongAlias<class ActivationSequenceTag, int>;

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_ACTIVATION_SEQUENCE_H_
