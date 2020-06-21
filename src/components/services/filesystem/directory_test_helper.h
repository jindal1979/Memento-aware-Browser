// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SERVICES_FILESYSTEM_DIRECTORY_TEST_HELPER_H_
#define COMPONENTS_SERVICES_FILESYSTEM_DIRECTORY_TEST_HELPER_H_

#include "base/macros.h"
#include "base/threading/sequence_bound.h"
#include "components/services/filesystem/public/mojom/directory.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace filesystem {

// Helper class for tests which want to use a remote DirectoryImpl. This binds
// DirectoryImpl instances for temporary directories on a background thread
// which supports blocking operations.
class DirectoryTestHelper {
 public:
  DirectoryTestHelper();
  ~DirectoryTestHelper();

  mojo::Remote<mojom::Directory> CreateTempDir();

 private:
  class BlockingState;

  base::SequenceBound<BlockingState> blocking_state_;

  DISALLOW_COPY_AND_ASSIGN(DirectoryTestHelper);
};

}  // namespace filesystem

#endif  // COMPONENTS_SERVICES_FILESYSTEM_DIRECTORY_TEST_HELPER_H_
