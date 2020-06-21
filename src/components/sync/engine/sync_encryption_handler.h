// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_ENGINE_SYNC_ENCRYPTION_HANDLER_H_
#define COMPONENTS_SYNC_ENGINE_SYNC_ENCRYPTION_HANDLER_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "components/sync/base/model_type.h"
#include "components/sync/base/passphrase_enums.h"
#include "components/sync/nigori/nigori.h"
#include "components/sync/protocol/sync.pb.h"

namespace syncer {

class Cryptographer;
class KeystoreKeysHandler;
enum class PassphraseType;

// Reasons due to which Cryptographer might require a passphrase.
enum PassphraseRequiredReason {
  REASON_ENCRYPTION = 1,               // The cryptographer requires a
                                       // passphrase for its first attempt at
                                       // encryption. Happens only during
                                       // migration or upgrade.
  REASON_DECRYPTION = 2,               // The cryptographer requires a
                                       // passphrase for its first attempt at
                                       // decryption.
};

// Enum used to distinguish which bootstrap encryption token is being updated.
enum BootstrapTokenType {
  PASSPHRASE_BOOTSTRAP_TOKEN,
  KEYSTORE_BOOTSTRAP_TOKEN
};

// Sync's encryption handler. Handles tracking encrypted types, ensuring the
// cryptographer encrypts with the proper key and has the most recent keybag,
// and keeps the nigori node up to date.
// Implementations of this class must be assumed to be non-thread-safe. All
// methods must be invoked on the sync thread.
// TODO(crbug.com/1010397): Rename this class.
class SyncEncryptionHandler {
 public:
  static constexpr PassphraseType kInitialPassphraseType =
      PassphraseType::kImplicitPassphrase;

  // All Observer methods are done synchronously from within a transaction and
  // on the sync thread.
  class Observer {
   public:
    Observer() = default;
    virtual ~Observer() = default;

    // Called when user interaction is required to obtain a valid passphrase.
    // - If the passphrase is required for encryption, |reason| will be
    //   REASON_ENCRYPTION.
    // - If the passphrase is required for the decryption of data that has
    //   already been encrypted, |reason| will be REASON_DECRYPTION.
    // - If the passphrase is required because decryption failed, and a new
    //   passphrase is required, |reason| will be REASON_SET_PASSPHRASE_FAILED.
    //
    // |key_derivation_params| are the parameters that should be used to obtain
    // the key from the passphrase.
    // |pending_keys| is a copy of the cryptographer's pending keys, that may be
    // cached by the frontend for subsequent use by the UI.
    virtual void OnPassphraseRequired(
        PassphraseRequiredReason reason,
        const KeyDerivationParams& key_derivation_params,
        const sync_pb::EncryptedData& pending_keys) = 0;

    // Called when the passphrase provided by the user has been accepted and is
    // now used to encrypt sync data.
    virtual void OnPassphraseAccepted() = 0;

    // Called when decryption keys are required in order to decrypt pending
    // Nigori keys and resume sync, for the TRUSTED_VAULT_PASSPHRASE case. This
    // can be resolved by calling AddTrustedVaultDecryptionKeys() with the
    // appropriate keys.
    virtual void OnTrustedVaultKeyRequired() = 0;

    // Called when the keys provided via AddTrustedVaultDecryptionKeys have been
    // accepted and there are no longer pending keys.
    virtual void OnTrustedVaultKeyAccepted() = 0;

    // |bootstrap_token| is an opaque base64 encoded representation of the key
    // generated by the current passphrase, and is provided to the observer for
    // persistence purposes and use in a future initialization of sync (e.g.
    // after restart). The boostrap token will always be derived from the most
    // recent GAIA password (for accounts with implicit passphrases), even if
    // the data is still encrypted with an older GAIA password. For accounts
    // with explicit passphrases, it will be the most recently seen custom
    // passphrase.
    virtual void OnBootstrapTokenUpdated(const std::string& bootstrap_token,
                                         BootstrapTokenType type) = 0;

    // Called when the set of encrypted types or the encrypt
    // everything flag has been changed.  Note that encryption isn't
    // complete until the OnEncryptionComplete() notification has been
    // sent (see below).
    //
    // |encrypted_types| will always be a superset of
    // AlwaysEncryptedUserTypes().  If |encrypt_everything| is
    // true, |encrypted_types| will be the set of all known types.
    //
    // Until this function is called, observers can assume that the
    // set of encrypted types is AlwaysEncryptedUserTypes() and that the
    // encrypt everything flag is false.
    virtual void OnEncryptedTypesChanged(ModelTypeSet encrypted_types,
                                         bool encrypt_everything) = 0;

    // Called after we finish encrypting the current set of encrypted
    // types.
    virtual void OnEncryptionComplete() = 0;

    // The cryptographer has been updated and/or the presence of pending keys
    // changed.
    virtual void OnCryptographerStateChanged(Cryptographer* cryptographer,
                                             bool has_pending_keys) = 0;

    // The passphrase type has changed. |type| is the new type,
    // |passphrase_time| is the time the passphrase was set (unset if |type|
    // is KEYSTORE_PASSPHRASE or the passphrase was set before we started
    // recording the time).
    virtual void OnPassphraseTypeChanged(PassphraseType type,
                                         base::Time passphrase_time) = 0;
  };

  SyncEncryptionHandler() = default;
  virtual ~SyncEncryptionHandler() = default;

  // Add/Remove SyncEncryptionHandler::Observers.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  // Reads the nigori node, updates internal state as needed, and, if an
  // empty/stale nigori node is detected, overwrites the existing
  // nigori node. Upon completion, if the cryptographer is still ready
  // attempts to re-encrypt all sync data. Returns false in case of error.
  // Note: This method is expensive (it iterates through all encrypted types),
  // so should only be used sparingly (e.g. on startup).
  // TODO(crbug.com/): Rename to something like NotifyStateToObservers() or
  // even delete this API altogether.
  virtual bool Init() = 0;

  // Attempts to re-encrypt encrypted data types using the passphrase provided.
  // Notifies observers of the result of the operation via OnPassphraseAccepted
  // or OnPassphraseRequired, updates the nigori node, and does re-encryption as
  // appropriate. If an explicit password has been set previously, we drop
  // subsequent requests to set a passphrase. |passphrase| shouldn't be empty.
  virtual void SetEncryptionPassphrase(const std::string& passphrase) = 0;

  // Provides a passphrase for decrypting the user's existing sync data.
  // Notifies observers of the result of the operation via OnPassphraseAccepted
  // or OnPassphraseRequired, updates the nigori node, and does re-encryption as
  // appropriate if there is a previously cached encryption passphrase. It is an
  // error to call this when we don't have pending keys. |passphrase| shouldn't
  // be empty.
  virtual void SetDecryptionPassphrase(const std::string& passphrase) = 0;

  // Analogous to SetDecryptionPassphrase but specifically for
  // TRUSTED_VAULT_PASSPHRASE: it provides new decryption keys that could
  // allow decrypting pending Nigori keys. Notifies observers of the result of
  // the operation via OnTrustedVaultKeyAccepted if the provided keys
  // successfully decrypted pending keys.
  virtual void AddTrustedVaultDecryptionKeys(
      const std::vector<std::vector<uint8_t>>& keys) = 0;

  // Enables encryption of all datatypes.
  virtual void EnableEncryptEverything() = 0;

  // Whether encryption of all datatypes is enabled. If false, only sensitive
  // types are encrypted.
  virtual bool IsEncryptEverythingEnabled() const = 0;

  // Returns the time when Nigori was migrated to keystore or when it was
  // initialized in case it happens after migration was introduced. Returns
  // base::Time() in case migration isn't completed.
  virtual base::Time GetKeystoreMigrationTime() const = 0;

  // Returns KeystoreKeysHandler, allowing to pass new keystore keys and to
  // check whether keystore keys need to be requested from the server.
  virtual KeystoreKeysHandler* GetKeystoreKeysHandler() = 0;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_ENGINE_SYNC_ENCRYPTION_HANDLER_H_
