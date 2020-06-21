// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/model_impl/processor_entity_tracker.h"

#include <utility>

#include "base/trace_event/memory_usage_estimator.h"
#include "components/sync/model_impl/processor_entity.h"
#include "components/sync/protocol/proto_memory_estimations.h"

namespace syncer {

ProcessorEntityTracker::ProcessorEntityTracker(
    const sync_pb::ModelTypeState& model_type_state,
    std::map<std::string, std::unique_ptr<sync_pb::EntityMetadata>>
        metadata_map)
    : model_type_state_(model_type_state) {
  DCHECK(model_type_state.initial_sync_done());
  for (auto& kv : metadata_map) {
    std::unique_ptr<ProcessorEntity> entity =
        ProcessorEntity::CreateFromMetadata(kv.first, std::move(*kv.second));
    const ClientTagHash client_tag_hash =
        ClientTagHash::FromHashed(entity->metadata().client_tag_hash());

    DCHECK(storage_key_to_tag_hash_.find(entity->storage_key()) ==
           storage_key_to_tag_hash_.end());
    DCHECK(entities_.find(client_tag_hash) == entities_.end());
    storage_key_to_tag_hash_[entity->storage_key()] = client_tag_hash;
    entities_[client_tag_hash] = std::move(entity);
  }
}

ProcessorEntityTracker::~ProcessorEntityTracker() = default;

bool ProcessorEntityTracker::AllStorageKeysPopulated() const {
  for (const auto& kv : entities_) {
    const ProcessorEntity* entity = kv.second.get();
    if (entity->storage_key().empty())
      return false;
  }
  if (entities_.size() != storage_key_to_tag_hash_.size()) {
    return false;
  }
  return true;
}

void ProcessorEntityTracker::ClearTransientSyncState() {
  for (const auto& kv : entities_) {
    kv.second->ClearTransientSyncState();
  }
}

size_t ProcessorEntityTracker::CountNonTombstoneEntries() const {
  size_t count = 0;
  for (const auto& kv : entities_) {
    if (!kv.second->metadata().is_deleted()) {
      ++count;
    }
  }
  return count;
}

ProcessorEntity* ProcessorEntityTracker::Add(const std::string& storage_key,
                                             const EntityData& data) {
  DCHECK(!data.client_tag_hash.value().empty());
  DCHECK(!GetEntityForTagHash(data.client_tag_hash));
  DCHECK(storage_key.empty() || storage_key_to_tag_hash_.find(storage_key) ==
                                    storage_key_to_tag_hash_.end());
  std::unique_ptr<ProcessorEntity> entity = ProcessorEntity::CreateNew(
      storage_key, data.client_tag_hash, data.id, data.creation_time);
  ProcessorEntity* entity_ptr = entity.get();
  entities_[data.client_tag_hash] = std::move(entity);
  if (!storage_key.empty())
    storage_key_to_tag_hash_[storage_key] = data.client_tag_hash;
  return entity_ptr;
}

void ProcessorEntityTracker::RemoveEntityForClientTagHash(
    const ClientTagHash& client_tag_hash) {
  DCHECK(model_type_state_.initial_sync_done());
  DCHECK(!client_tag_hash.value().empty());
  const ProcessorEntity* entity = GetEntityForTagHash(client_tag_hash);
  if (entity == nullptr || entity->storage_key().empty()) {
    entities_.erase(client_tag_hash);
  } else {
    DCHECK(storage_key_to_tag_hash_.find(entity->storage_key()) !=
           storage_key_to_tag_hash_.end());
    RemoveEntityForStorageKey(entity->storage_key());
  }
}

void ProcessorEntityTracker::RemoveEntityForStorageKey(
    const std::string& storage_key) {
  DCHECK(model_type_state_.initial_sync_done());
  // Look-up the client tag hash.
  auto iter = storage_key_to_tag_hash_.find(storage_key);
  if (iter == storage_key_to_tag_hash_.end()) {
    // Missing is as good as untracked as far as the model is concerned.
    return;
  }

  DCHECK_EQ(entities_[iter->second]->storage_key(), storage_key);
  entities_.erase(iter->second);
  storage_key_to_tag_hash_.erase(iter);
}

void ProcessorEntityTracker::ClearStorageKey(const std::string& storage_key) {
  DCHECK(!storage_key.empty());

  ProcessorEntity* entity = GetEntityForStorageKey(storage_key);
  DCHECK(entity);
  DCHECK_EQ(entity->storage_key(), storage_key);
  storage_key_to_tag_hash_.erase(storage_key);
  entity->ClearStorageKey();
}

size_t ProcessorEntityTracker::EstimateMemoryUsage() const {
  size_t memory_usage = 0;
  memory_usage += sync_pb::EstimateMemoryUsage(model_type_state_);
  memory_usage += base::trace_event::EstimateMemoryUsage(entities_);
  memory_usage +=
      base::trace_event::EstimateMemoryUsage(storage_key_to_tag_hash_);
  return memory_usage;
}

ProcessorEntity* ProcessorEntityTracker::GetEntityForTagHash(
    const ClientTagHash& tag_hash) {
  return const_cast<ProcessorEntity*>(
      static_cast<const ProcessorEntityTracker*>(this)->GetEntityForTagHash(
          tag_hash));
}

const ProcessorEntity* ProcessorEntityTracker::GetEntityForTagHash(
    const ClientTagHash& tag_hash) const {
  auto it = entities_.find(tag_hash);
  return it != entities_.end() ? it->second.get() : nullptr;
}

ProcessorEntity* ProcessorEntityTracker::GetEntityForStorageKey(
    const std::string& storage_key) {
  return const_cast<ProcessorEntity*>(
      static_cast<const ProcessorEntityTracker*>(this)->GetEntityForStorageKey(
          storage_key));
}

const ProcessorEntity* ProcessorEntityTracker::GetEntityForStorageKey(
    const std::string& storage_key) const {
  auto iter = storage_key_to_tag_hash_.find(storage_key);
  if (iter == storage_key_to_tag_hash_.end()) {
    return nullptr;
  }
  return GetEntityForTagHash(iter->second);
}

std::vector<const ProcessorEntity*>
ProcessorEntityTracker::GetAllEntitiesIncludingTombstones() const {
  std::vector<const ProcessorEntity*> entities;
  entities.reserve(entities_.size());
  for (const auto& entity : entities_) {
    entities.push_back(entity.second.get());
  }
  return entities;
}

std::vector<ProcessorEntity*>
ProcessorEntityTracker::GetEntitiesWithLocalChanges(size_t max_entries) {
  std::vector<ProcessorEntity*> entities;
  for (const auto& kv : entities_) {
    ProcessorEntity* entity = kv.second.get();
    if (entity->RequiresCommitRequest() && !entity->RequiresCommitData()) {
      entities.push_back(entity);
      if (entities.size() >= max_entries)
        break;
    }
  }
  return entities;
}

bool ProcessorEntityTracker::HasLocalChanges() const {
  for (const auto& kv : entities_) {
    ProcessorEntity* entity = kv.second.get();
    if (entity->RequiresCommitRequest()) {
      return true;
    }
  }
  return false;
}

size_t ProcessorEntityTracker::size() const {
  return entities_.size();
}

std::vector<const ProcessorEntity*>
ProcessorEntityTracker::IncrementSequenceNumberForAllExcept(
    const std::unordered_set<std::string>& already_updated_storage_keys) {
  std::vector<const ProcessorEntity*> affected_entities;
  for (const auto& kv : entities_) {
    ProcessorEntity* entity = kv.second.get();
    if (entity->storage_key().empty() ||
        (already_updated_storage_keys.find(entity->storage_key()) !=
         already_updated_storage_keys.end())) {
      // Entities with empty storage key were already processed. ProcessUpdate()
      // incremented their sequence numbers and cached commit data. Their
      // metadata will be persisted in UpdateStorageKey().
      continue;
    }
    entity->IncrementSequenceNumber(base::Time::Now());
    affected_entities.push_back(entity);
  }
  return affected_entities;
}

void ProcessorEntityTracker::UpdateOrOverrideStorageKey(
    const ClientTagHash& client_tag_hash,
    const std::string& storage_key) {
  ProcessorEntity* entity = GetEntityForTagHash(client_tag_hash);
  DCHECK(entity);
  // If the entity already had a storage key, clear it.
  const std::string previous_storage_key = entity->storage_key();
  DCHECK_NE(previous_storage_key, storage_key);
  if (!previous_storage_key.empty()) {
    ClearStorageKey(previous_storage_key);
  }
  DCHECK(storage_key_to_tag_hash_.find(previous_storage_key) ==
         storage_key_to_tag_hash_.end());
  // Populate the new storage key in the existing entity.
  entity->SetStorageKey(storage_key);
  DCHECK(storage_key_to_tag_hash_.find(storage_key) ==
         storage_key_to_tag_hash_.end());
  storage_key_to_tag_hash_[storage_key] = client_tag_hash;
}

}  // namespace syncer
