// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_EXTERNAL_DATA_HANDLERS_CROSTINI_ANSIBLE_PLAYBOOK_EXTERNAL_DATA_HANDLER_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_EXTERNAL_DATA_HANDLERS_CROSTINI_ANSIBLE_PLAYBOOK_EXTERNAL_DATA_HANDLER_H_

#include <memory>
#include <string>

#include "chrome/browser/chromeos/policy/external_data_handlers/cloud_external_data_policy_handler.h"

namespace chromeos {
class CrosSettings;
}  // namespace chromeos

namespace policy {

class DeviceLocalAccountPolicyService;

class CrostiniAnsiblePlaybookExternalDataHandler
    : public CloudExternalDataPolicyHandler {
 public:
  CrostiniAnsiblePlaybookExternalDataHandler(
      chromeos::CrosSettings* cros_settings,
      DeviceLocalAccountPolicyService* policy_service);
  ~CrostiniAnsiblePlaybookExternalDataHandler() override;

  // CloudExternalDataPolicyHandler:
  void OnExternalDataCleared(const std::string& policy,
                             const std::string& user_id) override;
  void OnExternalDataFetched(const std::string& policy,
                             const std::string& user_id,
                             std::unique_ptr<std::string> data,
                             const base::FilePath& file_path) override;
  void RemoveForAccountId(const AccountId& account_id) override;

 private:
  CloudExternalDataPolicyObserver crostini_ansible_observer_;

  DISALLOW_COPY_AND_ASSIGN(CrostiniAnsiblePlaybookExternalDataHandler);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_EXTERNAL_DATA_HANDLERS_CROSTINI_ANSIBLE_PLAYBOOK_EXTERNAL_DATA_HANDLER_H_
