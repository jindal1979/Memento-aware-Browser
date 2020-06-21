// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/test/fake_server/fake_server_nigori_helper.h"

#include <string>
#include <vector>

#include "components/sync/nigori/nigori_test_utils.h"
#include "components/sync/protocol/nigori_specifics.pb.h"
#include "components/sync/test/fake_server/fake_server.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fake_server {

bool GetServerNigori(FakeServer* fake_server,
                     sync_pb::NigoriSpecifics* nigori) {
  std::vector<sync_pb::SyncEntity> entity_list =
      fake_server->GetPermanentSyncEntitiesByModelType(syncer::NIGORI);
  if (entity_list.size() != 1U) {
    return false;
  }

  *nigori = entity_list[0].specifics().nigori();
  return true;
}

void SetNigoriInFakeServer(const sync_pb::NigoriSpecifics& nigori,
                           FakeServer* fake_server) {
  std::string nigori_entity_id =
      fake_server->GetTopLevelPermanentItemId(syncer::NIGORI);
  ASSERT_NE(nigori_entity_id, "");
  sync_pb::EntitySpecifics nigori_entity_specifics;
  *nigori_entity_specifics.mutable_nigori() = nigori;
  fake_server->ModifyEntitySpecifics(nigori_entity_id, nigori_entity_specifics);
}

void SetKeystoreNigoriInFakeServer(FakeServer* fake_server) {
  const std::vector<std::vector<uint8_t>>& keystore_keys =
      fake_server->GetKeystoreKeys();
  ASSERT_EQ(keystore_keys.size(), 1u);
  const syncer::KeyParamsForTesting keystore_key_params =
      syncer::Pbkdf2KeyParamsForTesting(keystore_keys.back());
  SetNigoriInFakeServer(syncer::BuildKeystoreNigoriSpecifics(
                            /*keybag_keys_params=*/{keystore_key_params},
                            /*keystore_decryptor_params=*/keystore_key_params,
                            /*keystore_key_params=*/keystore_key_params),
                        fake_server);
}

}  // namespace fake_server
