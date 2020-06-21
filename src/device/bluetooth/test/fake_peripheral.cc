// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/test/fake_peripheral.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "device/bluetooth/public/cpp/bluetooth_uuid.h"
#include "device/bluetooth/test/fake_remote_gatt_service.h"

namespace bluetooth {

FakePeripheral::FakePeripheral(FakeCentral* fake_central,
                               const std::string& address)
    : device::BluetoothDevice(fake_central),
      address_(address),
      system_connected_(false),
      gatt_connected_(false),
      last_service_id_(0),
      pending_gatt_discovery_(false) {}

FakePeripheral::~FakePeripheral() = default;

void FakePeripheral::SetName(base::Optional<std::string> name) {
  name_ = std::move(name);
}

void FakePeripheral::SetSystemConnected(bool connected) {
  system_connected_ = connected;
}

void FakePeripheral::SetServiceUUIDs(UUIDSet service_uuids) {
  device::BluetoothDevice::GattServiceMap gatt_services;
  bool inserted;

  // Create a temporary map of services, because ReplaceServiceUUIDs expects a
  // GattServiceMap even though it only uses the UUIDs.
  int count = 0;
  for (const auto& uuid : service_uuids) {
    std::string id = base::NumberToString(count++);
    std::tie(std::ignore, inserted) =
        gatt_services.emplace(id, std::make_unique<FakeRemoteGattService>(
                                      id, uuid, /*is_primary=*/true, this));
    DCHECK(inserted);
  }
  device_uuids_.ReplaceServiceUUIDs(gatt_services);
}

void FakePeripheral::SetNextGATTConnectionResponse(uint16_t code) {
  DCHECK(!next_connection_response_);
  DCHECK(create_gatt_connection_error_callbacks_.empty());
  next_connection_response_ = code;
}

void FakePeripheral::SetNextGATTDiscoveryResponse(uint16_t code) {
  DCHECK(!next_discovery_response_);
  next_discovery_response_ = code;
}

bool FakePeripheral::AllResponsesConsumed() {
  return !next_connection_response_ && !next_discovery_response_ &&
         std::all_of(gatt_services_.begin(), gatt_services_.end(),
                     [](const auto& e) {
                       FakeRemoteGattService* fake_remote_gatt_service =
                           static_cast<FakeRemoteGattService*>(e.second.get());
                       return fake_remote_gatt_service->AllResponsesConsumed();
                     });
}

void FakePeripheral::SimulateGATTDisconnection() {
  gatt_services_.clear();
  // TODO(crbug.com/728870): Only set get_connected_ to false once system
  // connected peripherals are supported and Web Bluetooth uses them. See issue
  // for more details.
  system_connected_ = false;
  gatt_connected_ = false;
  device_uuids_.ClearServiceUUIDs();
  SetGattServicesDiscoveryComplete(false);
  DidDisconnectGatt();
}

std::string FakePeripheral::AddFakeService(
    const device::BluetoothUUID& service_uuid) {
  // Attribute instance Ids need to be unique.
  std::string new_service_id =
      base::StringPrintf("%s_%zu", GetAddress().c_str(), ++last_service_id_);

  GattServiceMap::iterator it;
  bool inserted;

  std::tie(it, inserted) = gatt_services_.emplace(
      new_service_id,
      std::make_unique<FakeRemoteGattService>(new_service_id, service_uuid,
                                              true /* is_primary */, this));

  DCHECK(inserted);
  return it->second->GetIdentifier();
}

bool FakePeripheral::RemoveFakeService(const std::string& identifier) {
  auto it = gatt_services_.find(identifier);
  if (it == gatt_services_.end()) {
    return false;
  }

  gatt_services_.erase(it);
  return true;
}

uint32_t FakePeripheral::GetBluetoothClass() const {
  NOTREACHED();
  return 0;
}

#if defined(OS_CHROMEOS) || defined(OS_LINUX)
device::BluetoothTransport FakePeripheral::GetType() const {
  NOTREACHED();
  return device::BLUETOOTH_TRANSPORT_INVALID;
}
#endif

std::string FakePeripheral::GetIdentifier() const {
  NOTREACHED();
  return std::string();
}

std::string FakePeripheral::GetAddress() const {
  return address_;
}

device::BluetoothDevice::VendorIDSource FakePeripheral::GetVendorIDSource()
    const {
  NOTREACHED();
  return VENDOR_ID_UNKNOWN;
}

uint16_t FakePeripheral::GetVendorID() const {
  NOTREACHED();
  return 0;
}

uint16_t FakePeripheral::GetProductID() const {
  NOTREACHED();
  return 0;
}

uint16_t FakePeripheral::GetDeviceID() const {
  NOTREACHED();
  return 0;
}

uint16_t FakePeripheral::GetAppearance() const {
  NOTREACHED();
  return 0;
}

base::Optional<std::string> FakePeripheral::GetName() const {
  return name_;
}

base::string16 FakePeripheral::GetNameForDisplay() const {
  return base::string16();
}

bool FakePeripheral::IsPaired() const {
  NOTREACHED();
  return false;
}

bool FakePeripheral::IsConnected() const {
  NOTREACHED();
  return false;
}

bool FakePeripheral::IsGattConnected() const {
  // TODO(crbug.com/728870): Return gatt_connected_ only once system connected
  // peripherals are supported and Web Bluetooth uses them. See issue for more
  // details.
  return system_connected_ || gatt_connected_;
}

bool FakePeripheral::IsConnectable() const {
  NOTREACHED();
  return false;
}

bool FakePeripheral::IsConnecting() const {
  NOTREACHED();
  return false;
}

bool FakePeripheral::ExpectingPinCode() const {
  NOTREACHED();
  return false;
}

bool FakePeripheral::ExpectingPasskey() const {
  NOTREACHED();
  return false;
}

bool FakePeripheral::ExpectingConfirmation() const {
  NOTREACHED();
  return false;
}

void FakePeripheral::GetConnectionInfo(const ConnectionInfoCallback& callback) {
  NOTREACHED();
}

void FakePeripheral::SetConnectionLatency(ConnectionLatency connection_latency,
                                          const base::Closure& callback,
                                          const ErrorCallback& error_callback) {
  NOTREACHED();
}

void FakePeripheral::Connect(PairingDelegate* pairing_delegate,
                             base::OnceClosure callback,
                             ConnectErrorCallback error_callback) {
  NOTREACHED();
}

void FakePeripheral::SetPinCode(const std::string& pincode) {
  NOTREACHED();
}

void FakePeripheral::SetPasskey(uint32_t passkey) {
  NOTREACHED();
}

void FakePeripheral::ConfirmPairing() {
  NOTREACHED();
}

void FakePeripheral::RejectPairing() {
  NOTREACHED();
}

void FakePeripheral::CancelPairing() {
  NOTREACHED();
}

void FakePeripheral::Disconnect(const base::Closure& callback,
                                const ErrorCallback& error_callback) {
  NOTREACHED();
}

void FakePeripheral::Forget(const base::Closure& callback,
                            const ErrorCallback& error_callback) {
  NOTREACHED();
}

void FakePeripheral::ConnectToService(
    const device::BluetoothUUID& uuid,
    const ConnectToServiceCallback& callback,
    const ConnectToServiceErrorCallback& error_callback) {
  NOTREACHED();
}

void FakePeripheral::ConnectToServiceInsecurely(
    const device::BluetoothUUID& uuid,
    const ConnectToServiceCallback& callback,
    const ConnectToServiceErrorCallback& error_callback) {
  NOTREACHED();
}

void FakePeripheral::CreateGattConnection(
    GattConnectionCallback callback,
    ConnectErrorCallback error_callback,
    base::Optional<device::BluetoothUUID> service_uuid) {
  create_gatt_connection_success_callbacks_.push_back(std::move(callback));
  create_gatt_connection_error_callbacks_.push_back(std::move(error_callback));

  // TODO(crbug.com/728870): Stop overriding CreateGattConnection once
  // IsGattConnected() is fixed. See issue for more details.
  if (gatt_connected_)
    return DidConnectGatt();

  CreateGattConnectionImpl(std::move(service_uuid));
}

bool FakePeripheral::IsGattServicesDiscoveryComplete() const {
  const bool discovery_complete =
      BluetoothDevice::IsGattServicesDiscoveryComplete();
  DCHECK(!(pending_gatt_discovery_ && discovery_complete));

  // There is currently no method to intiate a Service Discovery procedure.
  // Web Bluetooth keeps a queue of pending getPrimaryServices() requests until
  // BluetoothAdapter::Observer::GattServicesDiscovered is called.
  // We use a call to IsGattServicesDiscoveryComplete as a signal that Web
  // Bluetooth needs to initiate a Service Discovery procedure and post
  // a task to call GattServicesDiscovered to simulate that the procedure has
  // completed.
  // TODO(crbug.com/729456): Remove this override and run
  // DiscoverGattServices() callback with next_discovery_response_ once
  // DiscoverGattServices() is implemented.
  if (!pending_gatt_discovery_ && !discovery_complete) {
    pending_gatt_discovery_ = true;
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(&FakePeripheral::DispatchDiscoveryResponse,
                                  weak_ptr_factory_.GetWeakPtr()));
  }

  return discovery_complete;
}

void FakePeripheral::CreateGattConnectionImpl(
    base::Optional<device::BluetoothUUID>) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&FakePeripheral::DispatchConnectionResponse,
                                weak_ptr_factory_.GetWeakPtr()));
}

void FakePeripheral::DispatchConnectionResponse() {
  DCHECK(next_connection_response_);

  uint16_t code = next_connection_response_.value();
  next_connection_response_.reset();

  if (code == mojom::kHCISuccess) {
    gatt_connected_ = true;
    DidConnectGatt();
  } else if (code == mojom::kHCIConnectionTimeout) {
    DidFailToConnectGatt(ERROR_FAILED);
  } else {
    DidFailToConnectGatt(ERROR_UNKNOWN);
  }
}

void FakePeripheral::DispatchDiscoveryResponse() {
  DCHECK(next_discovery_response_);

  uint16_t code = next_discovery_response_.value();
  next_discovery_response_.reset();

  pending_gatt_discovery_ = false;
  if (code == mojom::kHCISuccess) {
    device_uuids_.ReplaceServiceUUIDs(gatt_services_);
    SetGattServicesDiscoveryComplete(true);
    GetAdapter()->NotifyGattServicesDiscovered(this);
  } else {
    SetGattServicesDiscoveryComplete(false);
  }
}

void FakePeripheral::DisconnectGatt() {
}

#if defined(OS_CHROMEOS)
void FakePeripheral::ExecuteWrite(
    const base::Closure& callback,
    const ExecuteWriteErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void FakePeripheral::AbortWrite(const base::Closure& callback,
                                const AbortWriteErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}
#endif

}  // namespace bluetooth
