// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_NETWORK_NETWORK_STATE_HANDLER_H_
#define CHROMEOS_NETWORK_NETWORK_STATE_HANDLER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/component_export.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "chromeos/network/managed_state.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_handler_callbacks.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_type_pattern.h"
#include "chromeos/network/shill_property_handler.h"

namespace base {
class ListValue;
class Location;
class Value;
}  // namespace base

namespace chromeos {

class DeviceState;
class NetworkState;
class NetworkStateHandlerObserver;
class NetworkStateHandlerTest;

// Class for tracking the list of visible networks and their properties.
//
// This class maps essential properties from the connection manager (Shill) for
// each visible network. It is not used to change the properties of services or
// devices, only global (manager) properties.
//
// All getters return the currently cached properties. This class is expected to
// keep properties up to date by managing the appropriate Shill observers.
// It will invoke its own more specific observer methods when the specified
// changes occur.
//
// Some notes about NetworkState and GUIDs:
// * A NetworkState exists for all network services stored in a profile, and
//   all "visible" networks (physically connected networks like ethernet and
//   cellular or in-range wifi networks). If the network is stored in a profile,
//   NetworkState.IsInProfile() will return true.
// * "Visible" networks return true for NetworkState.visible().
// * All networks saved to a profile will have a saved GUID that is persistent
//   across sessions.
// * Networks that are not saved to a profile will have a GUID assigned when
//   the initial properties are received. The GUID will be consistent for
//   the duration of a session, even if the network drops out and returns.

class COMPONENT_EXPORT(CHROMEOS_NETWORK) NetworkStateHandler
    : public internal::ShillPropertyHandler::Listener {
 public:
  typedef std::vector<std::unique_ptr<ManagedState>> ManagedStateList;
  typedef std::vector<const NetworkState*> NetworkStateList;
  typedef std::vector<const DeviceState*> DeviceStateList;

  class TetherSortDelegate {
   public:
    // Sorts |tether_networks| according to the Tether component rules.
    // |tether_networks| contains only networks of type Tether.
    virtual void SortTetherNetworkList(
        ManagedStateList* tether_networks) const = 0;
  };

  enum TechnologyState {
    TECHNOLOGY_UNAVAILABLE,
    TECHNOLOGY_AVAILABLE,
    TECHNOLOGY_UNINITIALIZED,
    TECHNOLOGY_ENABLING,
    TECHNOLOGY_ENABLED,
    TECHNOLOGY_DISABLING,
    TECHNOLOGY_PROHIBITED
  };

  ~NetworkStateHandler() override;

  // Called just before destruction to give observers a chance to remove
  // themselves and disable any networking.
  void Shutdown();

  // Add/remove observers.
  void AddObserver(NetworkStateHandlerObserver* observer,
                   const base::Location& from_here);
  void RemoveObserver(NetworkStateHandlerObserver* observer,
                      const base::Location& from_here);
  bool HasObserver(NetworkStateHandlerObserver* observer);

  // Returns the state for technology |type|. Only
  // NetworkTypePattern::Primitive, ::Mobile, ::Ethernet, and ::Tether are
  // supported.
  TechnologyState GetTechnologyState(const NetworkTypePattern& type) const;
  bool IsTechnologyAvailable(const NetworkTypePattern& type) const {
    return GetTechnologyState(type) != TECHNOLOGY_UNAVAILABLE;
  }
  bool IsTechnologyEnabled(const NetworkTypePattern& type) const {
    return GetTechnologyState(type) == TECHNOLOGY_ENABLED;
  }
  bool IsTechnologyProhibited(const NetworkTypePattern& type) const {
    return GetTechnologyState(type) == TECHNOLOGY_PROHIBITED;
  }
  bool IsTechnologyUninitialized(const NetworkTypePattern& type) const {
    return GetTechnologyState(type) == TECHNOLOGY_UNINITIALIZED;
  }

  // Asynchronously sets the technology enabled property for |type|. Only
  // NetworkTypePattern::Primitive, ::Mobile and ::Ethernet are supported.
  // Note: Modifies Manager state. Calls |error_callback| on failure.
  void SetTechnologyEnabled(
      const NetworkTypePattern& type,
      bool enabled,
      const network_handler::ErrorCallback& error_callback);

  // Sets the Tether technology state. Because Tether networks do not represent
  // real Shill networks, this value must be set by the Tether component rather
  // than being generated by Shill. See TetherDeviceStateManager for more
  // details.
  void SetTetherTechnologyState(TechnologyState technology_state);

  // Sets the scanning state of the Tether technology. Since Tether network
  // scans are not actually performed as part of Shill, this value must be set
  // by the Tether component.
  void SetTetherScanState(bool is_scanning);

  // Asynchronously sets the list of prohibited technologies. The accepted
  // values are the shill network technology identifiers. See also
  // chromeos::onc::Validator::ValidateGlobalNetworkConfiguration().
  void SetProhibitedTechnologies(
      const std::vector<std::string>& prohibited_technologies,
      const network_handler::ErrorCallback& error_callback);

  // Finds and returns a device state by |device_path| or NULL if not found.
  const DeviceState* GetDeviceState(const std::string& device_path) const;

  // Finds and returns a device state by |type|. Returns NULL if not found.
  const DeviceState* GetDeviceStateByType(const NetworkTypePattern& type) const;

  // Returns true if any device of |type| is scanning.
  bool GetScanningByType(const NetworkTypePattern& type) const;

  // Finds and returns a network state by |service_path| or NULL if not found.
  // Note: NetworkState is frequently updated asynchronously, i.e. properties
  // are not always updated all at once. This will contain the most recent
  // value for each property. To receive notifications when a property changes,
  // observe this class and implement NetworkPropertyChanged().
  const NetworkState* GetNetworkState(const std::string& service_path) const;

  // Returns the default network (which includes VPNs) based on the Shill
  // Manager.DefaultNetwork property. Normally this is the same as
  // ConnectedNetworkByType(NetworkTypePattern::Default()), but the timing might
  // differ.
  const NetworkState* DefaultNetwork() const;

  // Returns the primary connected network matching |type|, otherwise null.
  const NetworkState* ConnectedNetworkByType(const NetworkTypePattern& type);

  // Returns the primary connecting network matching |type|, otherwise null.
  const NetworkState* ConnectingNetworkByType(const NetworkTypePattern& type);

  // Returns the primary active network of matching |type|, otherwise null.
  // See also GetActiveNetworkListByType.
  const NetworkState* ActiveNetworkByType(const NetworkTypePattern& type);

  // Like ConnectedNetworkByType() but returns any matching visible network or
  // NULL. Mostly useful for mobile networks where there is generally only one
  // network. Note: O(N).
  const NetworkState* FirstNetworkByType(const NetworkTypePattern& type);

  // Sets the |connect_requested_| property of a NetworkState for
  // |service_path| if it exists. This is used to inform the UI that a network
  // is connecting before the state is set in Shill. If |connect_requested| is
  // true, NetworkState::IsConnectingState() will return true. This will cause
  // the network to be sorted first and it will be part of the active list.
  void SetNetworkConnectRequested(const std::string& service_path,
                                  bool connect_requested);

  // Called from Chrome's network portal detector to indicate whether Chrome has
  // detected that the network is in a captive portal state. This may or may
  // not match the network's |is_captive_portal_| which is provided by Shill.
  void SetNetworkChromePortalDetected(const std::string& service_path,
                                      bool portal_detected);

  // Returns the aa:bb formatted hardware (MAC) address for the first connected
  // network matching |type|, or an empty string if none is connected.
  std::string FormattedHardwareAddressForType(const NetworkTypePattern& type);

  // Convenience method to call GetNetworkListByType(visible=true).
  void GetVisibleNetworkListByType(const NetworkTypePattern& type,
                                   NetworkStateList* list);

  // Convenience method for GetVisibleNetworkListByType(Default).
  void GetVisibleNetworkList(NetworkStateList* list);

  // Sets |list| to contain the list of networks with matching |type| and the
  // following properties:
  // |configured_only| - if true only include networks where IsInProfile is true
  // |visible_only| - if true only include networks in the visible Services list
  // |limit| - if > 0 limits the number of results.
  // The returned list contains a copy of NetworkState pointers which should not
  // be stored or used beyond the scope of the calling function (i.e. they may
  // later become invalid, but only on the UI thread). SortNetworkList() will be
  // called if necessary to provide the states in a convenient order (see
  // SortNetworkList for details).
  void GetNetworkListByType(const NetworkTypePattern& type,
                            bool configured_only,
                            bool visible_only,
                            size_t limit,
                            NetworkStateList* list);

  // Sets |list| to contain the active networks matching |type|. An 'active'
  // network is connecting or connected, and the first connected active network
  // is the primary or 'default' network providing connectivity (which may be a
  // VPN, use NetworkTypePattern::NonVirtual() to ignore VPNs). See
  // GetNetworkListByType for notes on |list| results.
  void GetActiveNetworkListByType(const NetworkTypePattern& type,
                                  NetworkStateList* list);

  // Finds and returns the NetworkState associated with |service_path| or NULL
  // if not found. If |configured_only| is true, only returns saved entries
  // (IsInProfile is true).
  const NetworkState* GetNetworkStateFromServicePath(
      const std::string& service_path,
      bool configured_only) const;

  // Finds and returns the NetworkState associated with |guid| or NULL if not
  // found. This returns all entries (IsInProfile() may be true or false).
  const NetworkState* GetNetworkStateFromGuid(const std::string& guid) const;

  // Creates a Tether NetworkState that has no underlying shill type or
  // service. When initially created, it does not actually represent a real
  // network. The |guid| provided must be non-empty. If a network with |guid|
  // already exists, this method will do nothing. Use the provided |guid| to
  // refer to and fetch this NetworkState in the future. Note that the
  // |has_connected_to_host| parameter refers to whether the current device has
  // already connected to the Tether host device providing this Tether network
  // in the past.
  void AddTetherNetworkState(const std::string& guid,
                             const std::string& name,
                             const std::string& carrier,
                             int battery_percentage,
                             int signal_strength,
                             bool has_connected_to_host);

  // Updates the Tether properties (carrier, battery percentage, and signal
  // strength) for a network which has already been added via
  // AddTetherNetworkState. Returns whether the update was successful.
  bool UpdateTetherNetworkProperties(const std::string& guid,
                                     const std::string& carrier,
                                     int battery_percentage,
                                     int signal_strength);

  // Updates whether the Tether network with GUID |guid| has connected to the
  // host device before, setting the value to true. Note that there is no way to
  // change this value back to false. If no network with GUID |guid| is
  // registered or if the network is registered and its HasConnectedToHost value
  // was already true, this function does nothing. Returns whether the value was
  // actually changed.
  bool SetTetherNetworkHasConnectedToHost(const std::string& guid);

  // Remove a Tether NetworkState, using the same |guid| passed to
  // AddTetherNetworkState(). If no network with GUID |guid| is registered, this
  // function does nothing. Returns whether the network was actually removed.
  bool RemoveTetherNetworkState(const std::string& guid);

  // Disassociates the Tether network specified by |tether_network_guid| from
  // its associated Wi-Fi network. Returns whether the networkd were
  // successfully disassociated.
  bool DisassociateTetherNetworkStateFromWifiNetwork(
      const std::string& tether_network_guid);

  // Inform NetworkStateHandler that the provided Tether network with the
  // provided guid |tether_network_guid| is associated with the Wi-Fi network
  // with the provided guid |wifi_network_guid|. This Wi-Fi network can now be
  // hidden in the UI, and the Tether network will act as its proxy. Returns
  // false if the association failed (e.g., one or both networks don't exist).
  bool AssociateTetherNetworkStateWithWifiNetwork(
      const std::string& tether_network_guid,
      const std::string& wifi_network_guid);

  // Set the connection_state of the Tether NetworkState corresponding to the
  // provided |guid| to "Disconnected". This will be reflected in the UI.
  void SetTetherNetworkStateDisconnected(const std::string& guid);

  // Set the connection_state of the Tether NetworkState corresponding to the
  // provided |guid| to "Connecting". This will be reflected in the UI.
  void SetTetherNetworkStateConnecting(const std::string& guid);

  // Set the connection_state of the Tether NetworkState corresponding to the
  // provided |guid| to "Connected". This will be reflected in the UI.
  void SetTetherNetworkStateConnected(const std::string& guid);

  void set_tether_sort_delegate(
      const TetherSortDelegate* tether_sort_delegate) {
    tether_sort_delegate_ = tether_sort_delegate;
  }

  // Sets |list| to contain the list of devices.  The returned list contains
  // a copy of DeviceState pointers which should not be stored or used beyond
  // the scope of the calling function (i.e. they may later become invalid, but
  // only on the UI thread).
  void GetDeviceList(DeviceStateList* list) const;

  // Like GetDeviceList() but only returns networks with matching |type|.
  void GetDeviceListByType(const NetworkTypePattern& type,
                           DeviceStateList* list) const;

  // Requests a network scan. This may trigger updates to the network
  // list, which will trigger the appropriate observer calls.
  // Note: If |type| is Cellular, a mobile network scan will be requested
  // if supported. This is disruptive and should only be triggered by an
  // explicit user action.
  void RequestScan(const NetworkTypePattern& type);

  // Requests an update for an existing NetworkState, e.g. after configuring
  // a network. This is a no-op if an update request is already pending. To
  // ensure that a change is picked up, this must be called after Shill
  // acknowledged it (e.g. in the callback of a SetProperties).
  // When the properties are received, NetworkPropertiesUpdated will be
  // signaled for each member of |observers_|, regardless of whether any
  // properties actually changed. Note that this is a no-op for Tether networks.
  void RequestUpdateForNetwork(const std::string& service_path);

  // Informs NetworkStateHandler to notify observers that the properties for
  // the network may have changed. Called e.g. when the proxy properties may
  // have changed.
  void SendUpdateNotificationForNetwork(const std::string& service_path);

  // Clears the last_error value for the NetworkState for |service_path|.
  void ClearLastErrorForNetwork(const std::string& service_path);

  // Sets the list of devices on which portal check is enabled.
  void SetCheckPortalList(const std::string& check_portal_list);

  // Sets a provider id (e.g. extension id) for a hex encoded SSID. If
  // |provider_id| is empty the entry will be erased.
  void SetCaptivePortalProviderForHexSsid(const std::string& hex_ssid,
                                          const std::string& provider_id,
                                          const std::string& provider_name);

  // Sets the Manager.WakeOnLan property. Note: we do not track this state, we
  // only set it.
  void SetWakeOnLanEnabled(bool enabled);

  // Sets the HostName property. Note: we do not track this property, we
  // only set it.
  void SetHostname(const std::string& hostname);

  // Enable or disable network bandwidth throttling, on all interfaces on the
  // system. If |enabled| is true, |upload_rate_kbits| and |download_rate_kbits|
  // are the desired rates (in kbits/s) to throttle to. If |enabled| is false,
  // throttling is off, and the rates are ignored.
  void SetNetworkThrottlingStatus(bool enabled,
                                  uint32_t upload_rate_kbits,
                                  uint32_t download_rate_kbits);

  // Sets the Fast Transition property. 802.11r Fast BSS Transition allows
  // wireless Access Points to share information before a device initiates a
  // reassociation. This allows devices to roam much more quickly.
  void SetFastTransitionStatus(bool enabled);

  const std::string& GetCheckPortalListForTest() const {
    return check_portal_list_;
  }

  // Returns the NetworkState for the EthernetEAP service, which contains the
  // EAP parameters used by the Ethernet network matching |service_path|, if it
  // exists. If |connected_only| is true, only returns the EthernetEAP state
  // if the Ethernet network is connected using EAP. Otherwise returns null.
  const NetworkState* GetEAPForEthernet(const std::string& service_path,
                                        bool connected_only);

  // Sets the |error_| property of the matching NetworkState for tests.
  void SetErrorForTest(const std::string& service_path,
                       const std::string& error);

  void SetDeviceStateUpdatedForTest(const std::string& device_path);

  // Sets |allow_only_policy_networks_to_connect_|,
  // |allow_only_policy_networks_to_connect_if_available_| and
  // |blacklisted_hex_ssids_| and calls |UpdateBlockedWifiNetworksInternal()|.
  virtual void UpdateBlockedWifiNetworks(
      bool only_managed,
      bool available_only,
      const std::vector<std::string>& blacklisted_hex_ssids);

  // Returns the NetworkState associated to the wifi device's
  // available_managed_network_path or |nullptr| if no managed network is
  // available.
  const NetworkState* GetAvailableManagedWifiNetwork() const;

  // Returns true if the AllowOnlyPolicyNetworksToConnect policy is enabled or
  // if the AllowOnlyPolicyNetworksToConnectIfAvailable policy is enabled and
  // there is a managed wifi network available.
  bool OnlyManagedWifiNetworksAllowed() const;

  bool default_network_is_metered() const {
    return default_network_is_metered_;
  }

  // Constructs and initializes an instance for testing.
  static std::unique_ptr<NetworkStateHandler> InitializeForTest();

  // Default set of comma separated interfaces on which to enable
  // portal checking.
  static const char kDefaultCheckPortalList[];

 protected:
  friend class NetworkHandler;
  NetworkStateHandler();

  // ShillPropertyHandler::Listener overrides.

  // This adds new entries to |network_list_| or |device_list_| and deletes any
  // entries that are no longer in the list.
  void UpdateManagedList(ManagedState::ManagedType type,
                         const base::ListValue& entries) override;

  // The list of profiles changed (i.e. a user has logged in). Re-request
  // properties for all services since they may have changed.
  void ProfileListChanged() override;

  // Parses the properties for the network service or device. Mostly calls
  // managed->PropertyChanged(key, value) for each dictionary entry.
  // |properties| is expected to be type DICTIONARY.
  void UpdateManagedStateProperties(ManagedState::ManagedType type,
                                    const std::string& path,
                                    const base::Value& properties) override;

  // Called by ShillPropertyHandler when a watched service property changes.
  void UpdateNetworkServiceProperty(const std::string& service_path,
                                    const std::string& key,
                                    const base::Value& value) override;

  // Called by ShillPropertyHandler when a watched device property changes.
  void UpdateDeviceProperty(const std::string& device_path,
                            const std::string& key,
                            const base::Value& value) override;

  // Called by ShillPropertyHandler when a watched network or device
  // IPConfig property changes. |properties| is expected to be type DICTIONARY.
  void UpdateIPConfigProperties(ManagedState::ManagedType type,
                                const std::string& path,
                                const std::string& ip_config_path,
                                const base::Value& properties) override;

  // Called by ShillPropertyHandler when the portal check list manager property
  // changes.
  void CheckPortalListChanged(const std::string& check_portal_list) override;

  // Called by ShillPropertyHandler when a technology list changes.
  void TechnologyListChanged() override;

  // Called by |shill_property_handler_| when the service or device list has
  // changed and all entries have been updated. This updates the list and
  // notifies observers.
  void ManagedStateListChanged(ManagedState::ManagedType type) override;

  // Called when the default network service changes. Sets default_network_path_
  // and notifies listeners.
  void DefaultNetworkServiceChanged(const std::string& service_path) override;

  // Called after construction. Called explicitly by tests after adding
  // test observers.
  void InitShillPropertyHandler();

 private:
  typedef std::map<std::string, std::string> SpecifierGuidMap;
  friend class NetworkStateHandlerTest;
  FRIEND_TEST_ALL_PREFIXES(NetworkStateHandlerTest, NetworkStateHandlerStub);
  FRIEND_TEST_ALL_PREFIXES(NetworkStateHandlerTest, BlockedByPolicyBlacklisted);
  FRIEND_TEST_ALL_PREFIXES(NetworkStateHandlerTest, BlockedByPolicyOnlyManaged);
  FRIEND_TEST_ALL_PREFIXES(NetworkStateHandlerTest,
                           BlockedByPolicyOnlyManagedIfAvailable);

  // Implementation for GetNetworkListByType and GetActiveNetworkListByType.
  void GetNetworkListByTypeImpl(const NetworkTypePattern& type,
                                bool configured_only,
                                bool visible_only,
                                bool active_only,
                                size_t limit,
                                NetworkStateList* list);

  // Sorts the network list. Called when all network updates have been received,
  // or when the network list is requested but the list is in an unsorted state.
  // Networks are sorted as follows, maintaining the existing relative ordering:
  // * Connected or connecting networks (should be listed first by Shill)
  // * Visible non-wifi networks
  // * Visible wifi networks
  // * Hidden (wifi) networks
  // If |ensure_cellular| is true, call EnsureCellularNetwork (which may
  // remove a network from the list).
  void SortNetworkList(bool ensure_cellular);

  // Updates UMA stats. Called once after all requested networks are updated.
  void UpdateNetworkStats();

  // NetworkState specific method for UpdateManagedStateProperties which
  // notifies observers. |properties| is expected to be type DICTIONARY.
  void UpdateNetworkStateProperties(NetworkState* network,
                                    const base::Value& properties);

  // Ensure a valid GUID for NetworkState.
  void UpdateGuid(NetworkState* network);

  // Look for a matching captive portal provider in
  // |hex_ssid_to_captive_portal_provider_map_|.
  void UpdateCaptivePortalProvider(NetworkState* network);

  // Update networkState properties from the associated DeviceState.
  void UpdateCellularStateFromDevice(NetworkState* network);

  // Cellular networks may not have an associated Shill Service (e.g. when the
  // SIM is locked or a mobile network is not available). This returns a new
  // default cellular network if necessary.
  std::unique_ptr<NetworkState> MaybeCreateDefaultCellularNetwork();

  // Removes the default Cellular network if it exists. Called when there is
  // more than one Cellular network in the list.
  void RemoveDefaultCellularNetwork();

  // Sends NetworkListChanged() to observers and logs an event.
  void NotifyNetworkListChanged();

  // Sends DeviceListChanged() to observers and logs an event.
  void NotifyDeviceListChanged();

  // Non-const getters for managed entries. These are const so that they can
  // be called by Get[Network|Device]State, even though they return non-const
  // pointers.
  DeviceState* GetModifiableDeviceState(const std::string& device_path) const;
  DeviceState* GetModifiableDeviceStateByType(
      const NetworkTypePattern& type) const;
  NetworkState* GetModifiableNetworkState(
      const std::string& service_path) const;
  NetworkState* GetModifiableNetworkStateFromGuid(
      const std::string& guid) const;
  ManagedState* GetModifiableManagedState(const ManagedStateList* managed_list,
                                          const std::string& path) const;

  // Gets the list specified by |type|.
  ManagedStateList* GetManagedList(ManagedState::ManagedType type);

  // Helper function that calls NotifyNetworkConnectionStateChanged and,
  // for the default network, OnDefaultNetworkConnectionStateChanged and
  // NotifyDefaultNetworkChanged.
  void OnNetworkConnectionStateChanged(NetworkState* network);

  // Verifies the connection state of the default network. Returns false
  // if the connection state change should be ignored.
  bool VerifyDefaultNetworkConnectionStateChange(NetworkState* network);

  // Notifies observers when a network's connection state changes.
  void NotifyNetworkConnectionStateChanged(NetworkState* network);

  // Notifies observers when the default network or its properties change.
  void NotifyDefaultNetworkChanged(const std::string& log_reason);

  // Notifies observers when the active state of any current or previously
  // active network changes, or the active networks order changes.
  bool ActiveNetworksChanged(const NetworkStateList& active_networks);
  void NotifyIfActiveNetworksChanged();

  // Notifies observers about changes to |network|, including IPConfg.
  void NotifyNetworkPropertiesUpdated(const NetworkState* network);

  // Notifies observers about changes to |device|, including IPConfigs.
  void NotifyDevicePropertiesUpdated(const DeviceState* device);

  // Called to ask observers to scan for networks.
  void NotifyScanRequested(const NetworkTypePattern& type);

  // Called whenever Device.Scanning state transitions to false.
  void NotifyScanCompleted(const DeviceState* device);

  // Helper function to log property updated events.
  void LogPropertyUpdated(const ManagedState* network,
                          const std::string& key,
                          const base::Value& value);

  // Returns one technology type for |type|. This technology will be the
  // highest priority technology in the type pattern.
  std::string GetTechnologyForType(const NetworkTypePattern& type) const;

  // Returns all the technology types for |type|.
  std::vector<std::string> GetTechnologiesForType(
      const NetworkTypePattern& type) const;

  // Adds Tether networks to |list|, limiting the maximum size of |list| to be
  // |limit|. If |get_active| is true, only active (i.e., connecting/connected)
  // networks will be added; otherwise, only inactive networks will be added.
  // The returned list contains a copy of NetworkState pointers which
  // should not be stored or used beyond the scope of the calling
  // function (i.e., they may later become invalid, but only on the UI thread).
  // See AddTetherNetworkState() for more information about Tether networks.
  void AppendTetherNetworksToList(bool get_active,
                                  size_t limit,
                                  NetworkStateList* list);

  // Set the connection_state of a Tether NetworkState corresponding to the
  // provided |guid|.
  void SetTetherNetworkStateConnectionState(
      const std::string& guid,
      const std::string& connection_state);

  // Ensures that the Tether DeviceState is present in |device_list_| if
  // |tether_technology_state_| is not TECHNOLOGY_UNAVAILABLE and ensures that
  // it is not present in |device_list_| if it is TECHNOLOGY_UNAVAILABLE.
  void EnsureTetherDeviceState();

  // Updates the network's |blocked_by_policy_| depending on
  // |allow_only_policy_networks_to_connect_| and |blacklisted_hex_ssids_|.
  // Returns true if the value changed.
  bool UpdateBlockedByPolicy(NetworkState* network) const;

  // Updates the device's |managed_network_available_| depending on the list of
  // networks associated with this device. Calls
  // |UpdateBlockedWifiNetworksInternal()| if the availability changed.
  void UpdateManagedWifiNetworkAvailable();

  // Calls |UpdateBlockedByPolicy()| for each wifi network.
  void UpdateBlockedWifiNetworksInternal();

  // Sets properties associated with the default network, currently the path and
  // Metered.
  void SetDefaultNetworkValues(const std::string& path, bool metered);

  // Shill property handler instance, owned by this class.
  std::unique_ptr<internal::ShillPropertyHandler> shill_property_handler_;

  // Observer list
  base::ObserverList<NetworkStateHandlerObserver, true>::Unchecked observers_;

  // List of managed network states
  ManagedStateList network_list_;

  // List of managed Tether network states, which exist separately from
  // |network_list_|.
  ManagedStateList tether_network_list_;

  // List of active networks, used to limit ActiveNetworksChanged events.
  class ActiveNetworkState;
  std::vector<ActiveNetworkState> active_network_list_;

  // Set to true when the network list is sorted, cleared when network updates
  // arrive. Used to trigger sorting when needed.
  bool network_list_sorted_ = false;

  // List of managed device states
  ManagedStateList device_list_;

  // Keeps track of the default network for notifying observers when it changes.
  // Do not set this directly, use SetDefaultNetworkValues() instead.
  std::string default_network_path_;

  // Tracks whether there is a connected default network and it is metered.
  // Do not set this directly, use SetDefaultNetworkValues() instead.
  bool default_network_is_metered_ = false;

  // List of interfaces on which portal check is enabled.
  std::string check_portal_list_;

  // Map of network specifiers to guids. Contains an entry for each
  // NetworkState that is not saved in a profile.
  SpecifierGuidMap specifier_guid_map_;

  // Map of hex SSIDs to captive portal provider info.
  // Used to set NetworkState::captive_portal_provider_.
  std::map<std::string, NetworkState::CaptivePortalProviderInfo>
      hex_ssid_to_captive_portal_provider_map_;

  // The state corresponding to the Tether device type. This value is managed by
  // the Tether component.
  TechnologyState tether_technology_state_ =
      TechnologyState::TECHNOLOGY_UNAVAILABLE;

  // Not owned by this instance.
  const TetherSortDelegate* tether_sort_delegate_ = nullptr;

  // Ensure that Shutdown() gets called exactly once.
  bool did_shutdown_ = false;

  // Ensure that we do not delete any networks while notifying observers.
  bool notifying_network_observers_ = false;

  // Policies which control WiFi blocking (Controlled from
  // |ManagedNetworkConfigurationHandler| by calling |UpdateBlockedNetworks()|).
  bool allow_only_policy_networks_to_connect_ = false;
  bool allow_only_policy_networks_to_connect_if_available_ = false;
  std::vector<std::string> blacklisted_hex_ssids_;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(NetworkStateHandler);
};

}  // namespace chromeos

#endif  // CHROMEOS_NETWORK_NETWORK_STATE_HANDLER_H_
