// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/arc/test/fake_clipboard_instance.h"
#include "base/bind_helpers.h"

namespace arc {

FakeClipboardInstance::FakeClipboardInstance() = default;

FakeClipboardInstance::~FakeClipboardInstance() = default;

void FakeClipboardInstance::Init(
    mojo::PendingRemote<mojom::ClipboardHost> host_remote,
    InitCallback callback) {
  std::move(callback).Run();
}

void FakeClipboardInstance::InitDeprecated(
    mojo::PendingRemote<mojom::ClipboardHost> host_remote) {
  Init(std::move(host_remote), base::DoNothing());
}

void FakeClipboardInstance::OnHostClipboardUpdated() {
  num_host_clipboard_updated_++;
}

}  // namespace arc
