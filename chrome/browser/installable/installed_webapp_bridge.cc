// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/installable/installed_webapp_bridge.h"

#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/jni_utils.h"
#include "base/feature_list.h"
#include "chrome/android/chrome_jni_headers/InstalledWebappBridge_jni.h"
#include "chrome/browser/flags/android/chrome_feature_list.h"
#include "url/gurl.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ScopedJavaLocalRef;

static void JNI_InstalledWebappBridge_NotifyPermissionsChange(JNIEnv* env,
                                                              jlong j_provider,
                                                              int type) {
  DCHECK_LT(type, static_cast<int32_t>(ContentSettingsType::NUM_TYPES));
  InstalledWebappProvider* provider =
    reinterpret_cast<InstalledWebappProvider*>(j_provider);
  provider->Notify(static_cast<ContentSettingsType>(type));
}

static void JNI_InstalledWebappBridge_NotifyPermissionResult(JNIEnv* env,
                                                             jlong callback_ptr,
                                                             jboolean allowed) {
  auto* callback =
      reinterpret_cast<InstalledWebappBridge::PermissionResponseCallback*>(
          callback_ptr);
  std::move(*callback).Run(allowed ? CONTENT_SETTING_ALLOW
                                   : CONTENT_SETTING_BLOCK);
  delete callback;
}

InstalledWebappProvider::RuleList
InstalledWebappBridge::GetInstalledWebappPermissions(
    ContentSettingsType content_type) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobjectArray> j_permissions =
      Java_InstalledWebappBridge_getPermissions(env,
                                                static_cast<int>(content_type));

  InstalledWebappProvider::RuleList rules;
  for (auto j_permission : j_permissions.ReadElements<jobject>()) {
    GURL origin(ConvertJavaStringToUTF8(
        Java_InstalledWebappBridge_getOriginFromPermission(env, j_permission)));
    ContentSetting setting = IntToContentSetting(
        Java_InstalledWebappBridge_getSettingFromPermission(env, j_permission));
    rules.push_back(std::make_pair(origin, setting));
  }

  return rules;
}

void InstalledWebappBridge::SetProviderInstance(
    InstalledWebappProvider *provider) {
  Java_InstalledWebappBridge_setInstalledWebappProvider(
      base::android::AttachCurrentThread(), (jlong) provider);
}

bool InstalledWebappBridge::ShouldDelegateLocationPermission(
    const GURL& origin_url) {
  if (!base::FeatureList::IsEnabled(
          chrome::android::kTrustedWebActivityLocationDelegation)) {
    return false;
  }

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jstring> j_origin =
      base::android::ConvertUTF8ToJavaString(env, origin_url.spec());
  jboolean result = Java_InstalledWebappBridge_shouldDelegateLocationPermission(
      env, j_origin);
  return result;
}

void InstalledWebappBridge::DecidePermission(
    const GURL& origin_url,
    PermissionResponseCallback callback) {
  JNIEnv* env = base::android::AttachCurrentThread();

  // Transfers the ownership of the callback to the Java callback. The Java
  // callback is guaranteed to be called unless the user never replies to the
  // dialog, but as the dialog is modal, the only other thing the user can do
  // is quit Chrome which will also free the pointer. The callback pointer will
  // be destroyed in NotifyPermissionResult.
  auto* callback_ptr = new PermissionResponseCallback(std::move(callback));

  ScopedJavaLocalRef<jstring> j_origin =
      base::android::ConvertUTF8ToJavaString(env, origin_url.spec());
  Java_InstalledWebappBridge_decidePermission(
      env, j_origin, reinterpret_cast<jlong>(callback_ptr));
}
