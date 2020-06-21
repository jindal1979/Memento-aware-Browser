// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/ios/account_select_fill_data.h"

#include "base/strings/string_util.h"
#include "components/autofill/core/common/password_form_fill_data.h"

using autofill::FieldRendererId;
using autofill::FormRendererId;

namespace password_manager {

FillData::FillData() = default;
FillData::~FillData() = default;

FormInfo::FormInfo() = default;
FormInfo::~FormInfo() = default;
FormInfo::FormInfo(const FormInfo&) = default;

Credential::Credential(const base::string16& username,
                       const base::string16& password,
                       const std::string& realm)
    : username(username), password(password), realm(realm) {}
Credential::~Credential() = default;

AccountSelectFillData::AccountSelectFillData() = default;
AccountSelectFillData::~AccountSelectFillData() = default;

void AccountSelectFillData::Add(
    const autofill::PasswordFormFillData& form_data) {
  auto iter_ok = forms_.insert(
      std::make_pair(form_data.form_renderer_id.value(), FormInfo()));
  FormInfo& form_info = iter_ok.first->second;
  form_info.origin = form_data.url;
  form_info.form_id = form_data.form_renderer_id;
  form_info.username_element_id = form_data.username_field.unique_renderer_id;
  form_info.password_element_id = form_data.password_field.unique_renderer_id;

  // Suggested credentials don't depend on a clicked form. It's better to use
  // the latest known credentials, since credentials can be updated between
  // loading of different forms.
  credentials_.clear();
  credentials_.push_back({form_data.username_field.value,
                          form_data.password_field.value,
                          form_data.preferred_realm});

  for (const auto& username_password_and_realm : form_data.additional_logins) {
    const base::string16& username = username_password_and_realm.username;
    const base::string16& password = username_password_and_realm.password;
    const std::string& realm = username_password_and_realm.realm;
    credentials_.push_back({username, password, realm});
  }
}

void AccountSelectFillData::Reset() {
  forms_.clear();
  credentials_.clear();
  last_requested_form_ = nullptr;
}

bool AccountSelectFillData::Empty() const {
  return credentials_.empty();
}

bool AccountSelectFillData::IsSuggestionsAvailable(
    FormRendererId form_identifier,
    FieldRendererId field_identifier,
    bool is_password_field) const {
  return GetFormInfo(form_identifier, field_identifier, is_password_field) !=
         nullptr;
}

std::vector<UsernameAndRealm> AccountSelectFillData::RetrieveSuggestions(
    FormRendererId form_identifier,
    FieldRendererId field_identifier,
    bool is_password_field) {
  last_requested_form_ =
      GetFormInfo(form_identifier, field_identifier, is_password_field);
  DCHECK(last_requested_form_);
  last_requested_password_field_id_ =
      is_password_field ? field_identifier : FieldRendererId();
  std::vector<UsernameAndRealm> result;
  for (const Credential& credential : credentials_)
    result.push_back({credential.username, credential.realm});

  return result;
}

std::unique_ptr<FillData> AccountSelectFillData::GetFillData(
    const base::string16& username) const {
  if (!last_requested_form_) {
    NOTREACHED();
    return nullptr;
  }

  auto it = std::find_if(credentials_.begin(), credentials_.end(),
                         [&username](const auto& credential) {
                           return credential.username == username;
                         });
  if (it == credentials_.end())
    return nullptr;
  const Credential& credential = *it;
  auto result = std::make_unique<FillData>();
  result->origin = last_requested_form_->origin;
  result->form_id = last_requested_form_->form_id;
  result->username_element_id = last_requested_form_->username_element_id;
  result->username_value = credential.username;
  result->password_element_id = last_requested_password_field_id_.is_null()
                                    ? last_requested_form_->password_element_id
                                    : last_requested_password_field_id_;
  result->password_value = credential.password;
  return result;
}

const FormInfo* AccountSelectFillData::GetFormInfo(
    FormRendererId form_identifier,
    FieldRendererId field_identifier,
    bool is_password_field) const {
  auto it = forms_.find(form_identifier);
  if (it == forms_.end())
    return nullptr;
  return is_password_field || it->second.username_element_id == field_identifier
             ? &it->second
             : nullptr;
}

}  // namespace  password_manager
