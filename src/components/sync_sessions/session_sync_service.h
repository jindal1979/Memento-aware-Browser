// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_SESSIONS_SESSION_SYNC_SERVICE_H_
#define COMPONENTS_SYNC_SESSIONS_SESSION_SYNC_SERVICE_H_

#include <memory>
#include <string>

#include "base/callback_list.h"
#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/sync/driver/data_type_controller.h"

namespace syncer {
class GlobalIdMapper;
class ModelTypeControllerDelegate;
}  // namespace syncer

namespace sync_sessions {

class OpenTabsUIDelegate;

// KeyedService responsible session sync (aka tab sync).
// This powers things like the history UI, where "Tabs from other devices"
// exists, as well as the uploading counterpart for other devices to see our
// local tabs.
class SessionSyncService : public KeyedService {
 public:
  SessionSyncService();
  ~SessionSyncService() override;

  virtual syncer::GlobalIdMapper* GetGlobalIdMapper() const = 0;

  // Return the active OpenTabsUIDelegate. If open/proxy tabs is not enabled or
  // not currently syncing, returns nullptr.
  virtual OpenTabsUIDelegate* GetOpenTabsUIDelegate() = 0;

  // Allows client code to be notified when foreign sessions change.
  virtual std::unique_ptr<base::CallbackList<void()>::Subscription>
  SubscribeToForeignSessionsChanged(const base::RepeatingClosure& cb)
      WARN_UNUSED_RESULT = 0;

  // For ProfileSyncService to initialize the controller for SESSIONS.
  virtual base::WeakPtr<syncer::ModelTypeControllerDelegate>
  GetControllerDelegate() = 0;

  // Intended to be used by ProxyDataTypeController: influences whether
  // GetOpenTabsUIDelegate() returns null or not.
  virtual void ProxyTabsStateChanged(
      syncer::DataTypeController::State state) = 0;

  // Used on Android only, to override the machine tag.
  virtual void SetSyncSessionsGUID(const std::string& guid) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(SessionSyncService);
};

}  // namespace sync_sessions

#endif  // COMPONENTS_SYNC_SESSIONS_SESSION_SYNC_SERVICE_H_
