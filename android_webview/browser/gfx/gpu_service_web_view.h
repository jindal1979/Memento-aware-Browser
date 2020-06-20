// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANDROID_WEBVIEW_BROWSER_GFX_GPU_SERVICE_WEB_VIEW_H_
#define ANDROID_WEBVIEW_BROWSER_GFX_GPU_SERVICE_WEB_VIEW_H_

#include <stddef.h>

#include <memory>
#include <utility>

#include "base/macros.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/config/gpu_info.h"
#include "gpu/config/gpu_preferences.h"

namespace gpu {
class MailboxManager;
class SyncPointManager;
class SharedImageManager;
}  // namespace gpu

namespace android_webview {

// This class acts like GpuServiceImpl for WebView. It owns gpu service objects
// and provides handle to these gpu objects for WebView. There is only one copy
// of this class in WebView.
class GpuServiceWebView {
 public:
  // This static function makes sure there is a single copy of this class.
  static GpuServiceWebView* GetInstance();
  ~GpuServiceWebView();

  gpu::SyncPointManager* sync_point_manager() const {
    return sync_point_manager_.get();
  }

  gpu::MailboxManager* mailbox_manager() const {
    return mailbox_manager_.get();
  }

  gpu::SharedImageManager* shared_image_manager() const {
    return shared_image_manager_.get();
  }

  const gpu::GPUInfo& gpu_info() const { return gpu_info_; }

  const gpu::GpuPreferences& gpu_preferences() const {
    return gpu_preferences_;
  }

  const gpu::GpuFeatureInfo& gpu_feature_info() const {
    return gpu_feature_info_;
  }

 private:
  // This function initialize GL using current command line, and construct gpu
  // service objects.
  static GpuServiceWebView* CreateGpuServiceWebView();
  GpuServiceWebView(
      std::unique_ptr<gpu::SyncPointManager> sync_pointer_manager,
      std::unique_ptr<gpu::MailboxManager> mailbox_manager,
      std::unique_ptr<gpu::SharedImageManager> shared_image_manager,
      const gpu::GPUInfo& gpu_info,
      const gpu::GpuPreferences& gpu_preferences,
      const gpu::GpuFeatureInfo& gpu_feature_info);

  std::unique_ptr<gpu::SyncPointManager> sync_point_manager_;
  std::unique_ptr<gpu::MailboxManager> mailbox_manager_;
  std::unique_ptr<gpu::SharedImageManager> shared_image_manager_;

  gpu::GPUInfo gpu_info_;
  gpu::GpuPreferences gpu_preferences_;
  gpu::GpuFeatureInfo gpu_feature_info_;

  DISALLOW_COPY_AND_ASSIGN(GpuServiceWebView);
};

}  // namespace android_webview

#endif  // ANDROID_WEBVIEW_BROWSER_GFX_GPU_SERVICE_WEB_VIEW_H_
