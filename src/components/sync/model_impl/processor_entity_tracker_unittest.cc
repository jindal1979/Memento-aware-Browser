// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/model_impl/processor_entity_tracker.h"

#include <utility>

#include "components/sync/model/metadata_batch.h"
#include "components/sync/model_impl/processor_entity.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

using testing::ElementsAre;
using testing::IsNull;
using testing::NotNull;
using testing::UnorderedElementsAre;

const char kEmptyStorageKey[] = "";
const char kStorageKey1[] = "key1";
const char kStorageKey2[] = "key2";

sync_pb::ModelTypeState GenerateModelTypeState() {
  sync_pb::ModelTypeState model_type_state;
  model_type_state.set_initial_sync_done(true);
  return model_type_state;
}

std::unique_ptr<sync_pb::EntityMetadata> GenerateMetadata(
    const std::string& storage_key,
    const ClientTagHash& client_tag_hash) {
  sync_pb::EntityMetadata metadata;
  metadata.set_creation_time(1);
  metadata.set_modification_time(1);
  metadata.set_client_tag_hash(client_tag_hash.value());
  metadata.set_specifics_hash("specifics_hash");
  return std::make_unique<sync_pb::EntityMetadata>(std::move(metadata));
}

std::unique_ptr<sync_pb::EntityMetadata> GenerateTombstoneMetadata(
    const std::string& storage_key,
    const ClientTagHash& client_tag_hash) {
  std::unique_ptr<sync_pb::EntityMetadata> metadata =
      GenerateMetadata(storage_key, client_tag_hash);
  metadata->set_is_deleted(true);
  metadata->set_base_specifics_hash(metadata->specifics_hash());
  metadata->clear_specifics_hash();
  return metadata;
}

EntityData GenerateEntityData(const std::string& storage_key,
                              const ClientTagHash& client_tag_hash) {
  EntityData entity_data;
  entity_data.client_tag_hash = client_tag_hash;
  entity_data.creation_time = base::Time::Now();
  entity_data.modification_time = entity_data.creation_time;
  entity_data.name = storage_key;
  // The tracker requires non-empty specifics with any data type.
  entity_data.specifics.mutable_preference();
  return entity_data;
}

class ProcessorEntityTrackerTest : public ::testing::Test {
 public:
  ProcessorEntityTrackerTest()
      : entity_tracker_(GenerateModelTypeState(), {}) {}
  ~ProcessorEntityTrackerTest() override = default;

  const ClientTagHash kClientTagHash1 =
      ClientTagHash::FromHashed("client_tag_hash_1");
  const ClientTagHash kClientTagHash2 =
      ClientTagHash::FromHashed("client_tag_hash_2");

 protected:
  ProcessorEntityTracker entity_tracker_;
};

TEST_F(ProcessorEntityTrackerTest, ShouldLoadFromMetadata) {
  EntityMetadataMap metadata_map;
  metadata_map.emplace(kStorageKey1,
                       GenerateMetadata(kStorageKey1, kClientTagHash1));
  metadata_map.emplace(
      kStorageKey2, GenerateTombstoneMetadata(kStorageKey2, kClientTagHash2));
  ProcessorEntityTracker entity_tracker(GenerateModelTypeState(),
                                        std::move(metadata_map));

  // Check some getters for the entity tracker.
  EXPECT_EQ(2u, entity_tracker.size());
  EXPECT_EQ(1u, entity_tracker.CountNonTombstoneEntries());
  EXPECT_TRUE(entity_tracker.model_type_state().initial_sync_done());
  EXPECT_TRUE(entity_tracker.AllStorageKeysPopulated());
  EXPECT_FALSE(entity_tracker.HasLocalChanges());

  // Check each entity thoroughly.
  const ProcessorEntity* entity =
      entity_tracker.GetEntityForStorageKey(kStorageKey1);
  ASSERT_THAT(entity, NotNull());
  EXPECT_EQ(entity, entity_tracker.GetEntityForTagHash(kClientTagHash1));

  EXPECT_EQ(kStorageKey1, entity->storage_key());
  EXPECT_EQ(1u, entity->metadata().creation_time());
  EXPECT_EQ(1u, entity->metadata().modification_time());
  EXPECT_EQ("specifics_hash", entity->metadata().specifics_hash());
  EXPECT_EQ(entity->metadata().client_tag_hash(), kClientTagHash1.value());
  EXPECT_FALSE(entity->metadata().is_deleted());

  const ProcessorEntity* tombstone_entity =
      entity_tracker.GetEntityForStorageKey(kStorageKey2);
  ASSERT_THAT(tombstone_entity, NotNull());
  EXPECT_EQ(kStorageKey2, tombstone_entity->storage_key());
  EXPECT_EQ(1u, tombstone_entity->metadata().creation_time());
  EXPECT_EQ(1u, tombstone_entity->metadata().modification_time());
  EXPECT_EQ("specifics_hash",
            tombstone_entity->metadata().base_specifics_hash());
  EXPECT_FALSE(tombstone_entity->metadata().has_specifics_hash());
  EXPECT_EQ(tombstone_entity->metadata().client_tag_hash(),
            kClientTagHash2.value());
  EXPECT_TRUE(tombstone_entity->metadata().is_deleted());

  const std::vector<const ProcessorEntity*> all_entities =
      entity_tracker.GetAllEntitiesIncludingTombstones();
  EXPECT_THAT(all_entities, UnorderedElementsAre(entity, tombstone_entity));
}

TEST_F(ProcessorEntityTrackerTest, ShouldAddNewEntity) {
  EntityData entity_data = GenerateEntityData(kStorageKey1, kClientTagHash1);
  const ProcessorEntity* entity =
      entity_tracker_.Add(kStorageKey1, entity_data);
  ASSERT_THAT(entity, NotNull());

  EXPECT_EQ(1u, entity_tracker_.size());
  EXPECT_EQ(1u, entity_tracker_.CountNonTombstoneEntries());
  EXPECT_EQ(entity,
            entity_tracker_.GetEntityForTagHash(entity_data.client_tag_hash));
  EXPECT_EQ(entity, entity_tracker_.GetEntityForStorageKey(kStorageKey1));
  EXPECT_FALSE(entity_tracker_.HasLocalChanges());
  EXPECT_EQ(kStorageKey1, entity->storage_key());
  EXPECT_EQ(entity->metadata().client_tag_hash(),
            entity_data.client_tag_hash.value());
  EXPECT_FALSE(entity->metadata().is_deleted());
}

TEST_F(ProcessorEntityTrackerTest, ShouldAddEntityWithoutStorageKey) {
  EntityData entity_data = GenerateEntityData(kStorageKey1, kClientTagHash1);
  const ProcessorEntity* entity =
      entity_tracker_.Add(kEmptyStorageKey, entity_data);
  ASSERT_THAT(entity, NotNull());

  // The entity should be available by the client tag hash only.
  EXPECT_EQ(kEmptyStorageKey, entity->storage_key());
  EXPECT_EQ(entity, entity_tracker_.GetEntityForTagHash(kClientTagHash1));

  // The empty storage key must not be used.
  EXPECT_THAT(entity_tracker_.GetEntityForStorageKey(kEmptyStorageKey),
              IsNull());

  EXPECT_EQ(1u, entity_tracker_.size());
  EXPECT_EQ(1u, entity_tracker_.CountNonTombstoneEntries());
  EXPECT_EQ(entity->metadata().client_tag_hash(),
            entity_data.client_tag_hash.value());
  EXPECT_FALSE(entity->metadata().is_deleted());

  // Check that tracker is waiting for the storage key to be populated.
  EXPECT_FALSE(entity_tracker_.AllStorageKeysPopulated());

  entity_tracker_.UpdateOrOverrideStorageKey(kClientTagHash1, kStorageKey1);
  EXPECT_EQ(entity, entity_tracker_.GetEntityForStorageKey(kStorageKey1));
  EXPECT_EQ(1u, entity_tracker_.size());
  EXPECT_EQ(1u, entity_tracker_.CountNonTombstoneEntries());

  EXPECT_TRUE(entity_tracker_.AllStorageKeysPopulated());
}

TEST_F(ProcessorEntityTrackerTest, ShouldClearStorageKeyForTombstone) {
  ProcessorEntity* entity = entity_tracker_.Add(
      kStorageKey1, GenerateEntityData(kStorageKey1, kClientTagHash1));
  ASSERT_EQ(entity, entity_tracker_.GetEntityForStorageKey(kStorageKey1));
  ASSERT_EQ(kStorageKey1, entity->storage_key());

  // Mark the entity as removed.
  entity->Delete();
  ASSERT_EQ(1u, entity_tracker_.size());
  ASSERT_EQ(0u, entity_tracker_.CountNonTombstoneEntries());

  entity_tracker_.ClearStorageKey(kStorageKey1);
  EXPECT_THAT(entity_tracker_.GetEntityForStorageKey(kStorageKey1), IsNull());
  EXPECT_TRUE(entity->storage_key().empty());
  EXPECT_EQ(1u, entity_tracker_.size());
  EXPECT_EQ(0u, entity_tracker_.CountNonTombstoneEntries());
}

TEST_F(ProcessorEntityTrackerTest, ShouldOverrideTombstone) {
  ProcessorEntity* entity = entity_tracker_.Add(
      kStorageKey1, GenerateEntityData(kStorageKey1, kClientTagHash1));
  ASSERT_THAT(entity, NotNull());
  ASSERT_EQ(entity, entity_tracker_.GetEntityForStorageKey(kStorageKey1));
  ASSERT_EQ(kStorageKey1, entity->storage_key());

  // Mark the entity as removed.
  entity->Delete();
  ASSERT_EQ(1u, entity_tracker_.size());
  ASSERT_EQ(0u, entity_tracker_.CountNonTombstoneEntries());

  // Mimic an entity being created with the same client tag hash.
  entity_tracker_.UpdateOrOverrideStorageKey(kClientTagHash1, kStorageKey2);
  EXPECT_EQ(kStorageKey2, entity->storage_key());
  EXPECT_THAT(entity_tracker_.GetEntityForStorageKey(kStorageKey1), IsNull());
  EXPECT_EQ(entity, entity_tracker_.GetEntityForStorageKey(kStorageKey2));
  EXPECT_EQ(1u, entity_tracker_.size());
  EXPECT_EQ(0u, entity_tracker_.CountNonTombstoneEntries());
}

TEST_F(ProcessorEntityTrackerTest, ShouldRemoveEntityForStorageKey) {
  const ProcessorEntity* entity = entity_tracker_.Add(
      kStorageKey1, GenerateEntityData(kStorageKey1, kClientTagHash1));
  ASSERT_THAT(entity, NotNull());
  ASSERT_EQ(1u, entity_tracker_.size());

  entity_tracker_.RemoveEntityForStorageKey(kStorageKey1);
  EXPECT_EQ(0u, entity_tracker_.size());
}

TEST_F(ProcessorEntityTrackerTest, ShouldRemoveEntityForClientTagHash) {
  const ProcessorEntity* entity = entity_tracker_.Add(
      kStorageKey1, GenerateEntityData(kStorageKey1, kClientTagHash1));
  ASSERT_THAT(entity, NotNull());
  ASSERT_EQ(entity, entity_tracker_.GetEntityForTagHash(kClientTagHash1));

  const ProcessorEntity* entity_no_key = entity_tracker_.Add(
      kEmptyStorageKey, GenerateEntityData(kStorageKey2, kClientTagHash2));
  ASSERT_THAT(entity_no_key, NotNull());
  ASSERT_EQ(entity_no_key,
            entity_tracker_.GetEntityForTagHash(kClientTagHash2));
  ASSERT_EQ(2u, entity_tracker_.size());

  entity_tracker_.RemoveEntityForClientTagHash(kClientTagHash2);
  EXPECT_EQ(1u, entity_tracker_.size());
  EXPECT_THAT(entity_tracker_.GetEntityForTagHash(kClientTagHash2), IsNull());

  // A second call does not affect anything.
  entity_tracker_.RemoveEntityForClientTagHash(kClientTagHash2);
  EXPECT_EQ(1u, entity_tracker_.size());

  entity_tracker_.RemoveEntityForClientTagHash(kClientTagHash1);
  EXPECT_EQ(0u, entity_tracker_.size());
}

TEST_F(ProcessorEntityTrackerTest, ShouldReturnLocalChanges) {
  ProcessorEntity* entity = entity_tracker_.Add(
      kStorageKey1, GenerateEntityData(kStorageKey1, kClientTagHash1));
  ASSERT_THAT(entity, NotNull());
  ASSERT_FALSE(entity->IsUnsynced());
  ASSERT_FALSE(entity_tracker_.HasLocalChanges());
  ASSERT_TRUE(
      entity_tracker_.GetEntitiesWithLocalChanges(/*max_entries=*/1).empty());

  // Mark the entity as ready to commit.
  entity->MakeLocalChange(std::make_unique<EntityData>(
      GenerateEntityData(kStorageKey1, kClientTagHash1)));
  entity_tracker_.IncrementSequenceNumberForAllExcept({});
  EXPECT_TRUE(entity->IsUnsynced());
  EXPECT_TRUE(entity_tracker_.HasLocalChanges());
  EXPECT_THAT(entity_tracker_.GetEntitiesWithLocalChanges(/*max_entries=*/2),
              ElementsAre(entity));
}

}  // namespace

}  // namespace syncer
