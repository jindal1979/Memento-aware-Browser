// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_SECURE_CHANNEL_CONNECTION_ROLE_H_
#define CHROMEOS_SERVICES_SECURE_CHANNEL_CONNECTION_ROLE_H_

#include <ostream>

namespace chromeos {

namespace secure_channel {

// Enumeration of roles which can be used for a connection.
enum class ConnectionRole {
  // Initiates a connection to a remote device, which must be in the listener
  // role.
  kInitiatorRole,

  // Listens for incoming connections from remote devices in the initiator role.
  kListenerRole
};

std::ostream& operator<<(std::ostream& stream, const ConnectionRole& role);

}  // namespace secure_channel

}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_SECURE_CHANNEL_CONNECTION_ROLE_H_
