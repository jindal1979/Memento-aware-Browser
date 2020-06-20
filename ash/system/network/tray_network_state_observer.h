// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_NETWORK_TRAY_NETWORK_STATE_OBSERVER_H_
#define ASH_SYSTEM_NETWORK_TRAY_NETWORK_STATE_OBSERVER_H_

#include "base/observer_list_types.h"

namespace ash {

class TrayNetworkStateObserver : public base::CheckedObserver {
 public:
  // The active networks changed or a device enabled state changed.
  virtual void ActiveNetworkStateChanged() {}

  // The list of networks changed. The frequency of this event is limited.
  virtual void NetworkListChanged() {}

  // The list of VPN providers changed.
  virtual void VpnProvidersChanged() {}
};

}  // namespace ash

#endif  // ASH_SYSTEM_NETWORK_TRAY_NETWORK_STATE_OBSERVER_H_
