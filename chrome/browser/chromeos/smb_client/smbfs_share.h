// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SMB_CLIENT_SMBFS_SHARE_H_
#define CHROME_BROWSER_CHROMEOS_SMB_CLIENT_SMBFS_SHARE_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/smb_client/smb_errors.h"
#include "chrome/browser/chromeos/smb_client/smb_url.h"
#include "chromeos/components/smbfs/smbfs_host.h"
#include "chromeos/components/smbfs/smbfs_mounter.h"

class Profile;

namespace chromeos {
namespace smb_client {

// Represents an SMB share mounted using smbfs. Handles mounting, unmounting,
// registration, and IPC communication with filesystem.
// Destroying will unmount and deregister the filesystem.
class SmbFsShare : public smbfs::SmbFsHost::Delegate {
 public:
  using KerberosOptions = smbfs::SmbFsMounter::KerberosOptions;
  using MountOptions = smbfs::SmbFsMounter::MountOptions;
  using MountCallback = base::OnceCallback<void(SmbMountResult)>;
  using UnmountCallback = base::OnceCallback<void(chromeos::MountError)>;
  using RemoveCredentialsCallback = base::OnceCallback<void(bool)>;
  using DeleteRecursivelyCallback = base::OnceCallback<void(base::File::Error)>;
  using MounterCreationCallback =
      base::RepeatingCallback<std::unique_ptr<smbfs::SmbFsMounter>(
          const std::string& share_path,
          const std::string& mount_dir_name,
          const MountOptions& options,
          smbfs::SmbFsHost::Delegate* delegate)>;

  SmbFsShare(Profile* profile,
             const SmbUrl& share_url,
             const std::string& display_name,
             const MountOptions& options);
  ~SmbFsShare() override;

  SmbFsShare(const SmbFsShare&) = delete;
  SmbFsShare& operator=(const SmbFsShare&) = delete;

  // Mounts the SMB filesystem with |options_| and runs |callback| when
  // completed. Must not be called while mounted or another mount request is in
  // progress.
  void Mount(MountCallback callback);

  // Remount an unmounted SMB filesystem with |options| and run |callback|
  // when completed. |options_| will be updated by |options|.
  void Remount(const MountOptions& options, MountCallback callback);

  // Unmounts the filesystem and cancels any pending mount request.
  void Unmount(UnmountCallback callback);

  // Allow smbfs to make credentials request for a short period of time
  // (currently 5 seconds).
  void AllowCredentialsRequest();

  // Request that any credentials saved by smbfs are deleted.
  void RemoveSavedCredentials(RemoveCredentialsCallback callback);

  // Recursively delete |path| by making a Mojo request to smbfs.
  void DeleteRecursively(const base::FilePath& path,
                         DeleteRecursivelyCallback callback);

  // Returns whether the filesystem is mounted and accessible via mount_path().
  bool IsMounted() const { return bool(host_); }

  const std::string& mount_id() const { return mount_id_; }
  const SmbUrl& share_url() const { return share_url_; }
  const MountOptions& options() const { return options_; }

  base::FilePath mount_path() const {
    return host_ ? host_->mount_path() : base::FilePath();
  }

  void SetMounterCreationCallbackForTest(MounterCreationCallback callback);

 private:
  // Callback for smbfs::SmbFsMounter::Mount().
  void OnMountDone(MountCallback callback,
                   smbfs::mojom::MountError mount_error,
                   std::unique_ptr<smbfs::SmbFsHost> smbfs_host);

  // Called after cros-disks has attempted to unmount the share.
  void OnUnmountDone(SmbFsShare::UnmountCallback callback,
                     chromeos::MountError result);

  // Callback for smb_dialog::SmbCredentialsDialog::Show().
  void OnSmbCredentialsDialogShowDone(RequestCredentialsCallback callback,
                                      bool canceled,
                                      const std::string& username,
                                      const std::string& password);

  // Callback for smbfs::SmbFsHost::RemoveSavedCredentials().
  void OnRemoveSavedCredentialsDone(bool success);

  // Callback for smbfs::SmbFsHost::DeleteRecursively().
  void OnDeleteRecursivelyDone(base::File::Error error);

  // smbfs::SmbFsHost::Delegate overrides:
  void OnDisconnected() override;
  void RequestCredentials(RequestCredentialsCallback callback) override;

  Profile* const profile_;
  const SmbUrl share_url_;
  const std::string display_name_;
  MountOptions options_;
  const std::string mount_id_;
  bool unmount_pending_ = false;
  RemoveCredentialsCallback remove_credentials_callback_;
  DeleteRecursivelyCallback delete_recursively_callback_;

  MounterCreationCallback mounter_creation_callback_for_test_;
  std::unique_ptr<smbfs::SmbFsMounter> mounter_;
  std::unique_ptr<smbfs::SmbFsHost> host_;

  base::TimeTicks allow_credential_request_expiry_;
  bool allow_credential_request_ = false;

  base::WeakPtrFactory<SmbFsShare> weak_factory_{this};
};

}  // namespace smb_client
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_SMB_CLIENT_SMBFS_SHARE_H_
