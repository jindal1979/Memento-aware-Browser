// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/media_capture_util.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/callback.h"
#include "base/check.h"
#include "content/public/browser/media_capture_devices.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/permissions_data.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-shared.h"

using blink::MediaStreamDevice;
using blink::MediaStreamDevices;
using content::MediaCaptureDevices;
using content::MediaStreamUI;

namespace extensions {

namespace {

const MediaStreamDevice* GetRequestedDeviceOrDefault(
    const MediaStreamDevices& devices,
    const std::string& requested_device_id) {
  if (!requested_device_id.empty()) {
    auto it =
        std::find_if(devices.begin(), devices.end(),
                     [requested_device_id](const MediaStreamDevice& device) {
                       return device.id == requested_device_id;
                     });
    return it != devices.end() ? &(*it) : nullptr;
  }

  if (!devices.empty())
    return &devices[0];

  return nullptr;
}

}  // namespace

namespace media_capture_util {

// See also Chrome's MediaCaptureDevicesDispatcher.
void GrantMediaStreamRequest(content::WebContents* web_contents,
                             const content::MediaStreamRequest& request,
                             content::MediaResponseCallback callback,
                             const Extension* extension) {
  // app_shell only supports audio and video capture, not tab or screen capture.
  DCHECK(request.audio_type ==
             blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE ||
         request.video_type ==
             blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE);

  MediaStreamDevices devices;

  if (request.audio_type ==
      blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    VerifyMediaAccessPermission(request.audio_type, extension);
    const MediaStreamDevice* device = GetRequestedDeviceOrDefault(
        MediaCaptureDevices::GetInstance()->GetAudioCaptureDevices(),
        request.requested_audio_device_id);
    if (device)
      devices.push_back(*device);
  }

  if (request.video_type ==
      blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE) {
    VerifyMediaAccessPermission(request.video_type, extension);
    const MediaStreamDevice* device = GetRequestedDeviceOrDefault(
        MediaCaptureDevices::GetInstance()->GetVideoCaptureDevices(),
        request.requested_video_device_id);
    if (device)
      devices.push_back(*device);
  }

  // TODO(jamescook): Should we show a recording icon somewhere? If so, where?
  std::unique_ptr<MediaStreamUI> ui;
  std::move(callback).Run(
      devices,
      devices.empty() ? blink::mojom::MediaStreamRequestResult::INVALID_STATE
                      : blink::mojom::MediaStreamRequestResult::OK,
      std::move(ui));
}

void VerifyMediaAccessPermission(blink::mojom::MediaStreamType type,
                                 const Extension* extension) {
  const PermissionsData* permissions_data = extension->permissions_data();
  if (type == blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    // app_shell has no UI surface to show an error, and on an embedded device
    // it's better to crash than to have a feature not work.
    CHECK(permissions_data->HasAPIPermission(APIPermission::kAudioCapture))
        << "Audio capture request but no audioCapture permission in manifest.";
  } else {
    DCHECK(type == blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE);
    CHECK(permissions_data->HasAPIPermission(APIPermission::kVideoCapture))
        << "Video capture request but no videoCapture permission in manifest.";
  }
}

bool CheckMediaAccessPermission(blink::mojom::MediaStreamType type,
                                const Extension* extension) {
  const PermissionsData* permissions_data = extension->permissions_data();
  if (type == blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    return permissions_data->HasAPIPermission(APIPermission::kAudioCapture);
  }
  DCHECK(type == blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE);
  return permissions_data->HasAPIPermission(APIPermission::kVideoCapture);
}

}  // namespace media_capture_util
}  // namespace extensions
