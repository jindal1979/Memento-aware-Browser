// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "components/embedder_support/android/browser_context/browser_context_handle.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/android/jni_headers/UserPrefs_jni.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

namespace user_prefs {

static base::android::ScopedJavaLocalRef<jobject> JNI_UserPrefs_Get(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jbrowser_context_handle) {
  return UserPrefs::Get(browser_context::BrowserContextFromJavaHandle(
                            jbrowser_context_handle))
      ->GetJavaObject();
}

}  // namespace user_prefs
