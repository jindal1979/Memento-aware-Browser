// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_MODEL_IMPL_CLIENT_TAG_BASED_MODEL_TYPE_PROCESSOR_H_
#define COMPONENTS_SYNC_MODEL_IMPL_CLIENT_TAG_BASED_MODEL_TYPE_PROCESSOR_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/sequence_checker.h"
#include "components/sync/base/client_tag_hash.h"
#include "components/sync/base/model_type.h"
#include "components/sync/base/sync_stop_metadata_fate.h"
#include "components/sync/engine/cycle/status_counters.h"
#include "components/sync/engine/model_type_processor.h"
#include "components/sync/engine/non_blocking_sync_common.h"
#include "components/sync/model/data_batch.h"
#include "components/sync/model/data_type_activation_request.h"
#include "components/sync/model/metadata_batch.h"
#include "components/sync/model/metadata_change_list.h"
#include "components/sync/model/model_error.h"
#include "components/sync/model/model_type_change_processor.h"
#include "components/sync/model/model_type_sync_bridge.h"
#include "components/sync/model_impl/processor_entity_tracker.h"
#include "components/sync/protocol/model_type_state.pb.h"
#include "components/sync/protocol/sync.pb.h"

namespace syncer {

class CommitQueue;
class ProcessorEntity;

// A sync component embedded on the model type's thread that tracks entity
// metadata in the model store and coordinates communication between sync and
// model type threads. All changes in flight (either incoming from the server
// or local changes reported by the bridge) must specify a client tag.
//
// See //docs/sync/uss/client_tag_based_model_type_processor.md for a more
// thorough description.
class ClientTagBasedModelTypeProcessor : public ModelTypeProcessor,
                                         public ModelTypeChangeProcessor,
                                         public ModelTypeControllerDelegate {
 public:
  ClientTagBasedModelTypeProcessor(ModelType type,
                                   const base::RepeatingClosure& dump_stack);
  // Used only for unit-tests.
  ClientTagBasedModelTypeProcessor(ModelType type,
                                   const base::RepeatingClosure& dump_stack,
                                   bool commit_only);
  ~ClientTagBasedModelTypeProcessor() override;

  // Returns true if the handshake with sync thread is complete.
  bool IsConnected() const;

  // ModelTypeChangeProcessor implementation.
  void Put(const std::string& storage_key,
           std::unique_ptr<EntityData> entity_data,
           MetadataChangeList* metadata_change_list) override;
  void Delete(const std::string& storage_key,
              MetadataChangeList* metadata_change_list) override;
  void UpdateStorageKey(const EntityData& entity_data,
                        const std::string& storage_key,
                        MetadataChangeList* metadata_change_list) override;
  void UntrackEntityForStorageKey(const std::string& storage_key) override;
  void UntrackEntityForClientTagHash(
      const ClientTagHash& client_tag_hash) override;
  bool IsEntityUnsynced(const std::string& storage_key) override;
  base::Time GetEntityCreationTime(
      const std::string& storage_key) const override;
  base::Time GetEntityModificationTime(
      const std::string& storage_key) const override;
  void OnModelStarting(ModelTypeSyncBridge* bridge) override;
  void ModelReadyToSync(std::unique_ptr<MetadataBatch> batch) override;
  bool IsTrackingMetadata() override;
  std::string TrackedAccountId() override;
  std::string TrackedCacheGuid() override;
  void ReportError(const ModelError& error) override;
  base::Optional<ModelError> GetError() const override;
  base::WeakPtr<ModelTypeControllerDelegate> GetControllerDelegate() override;

  // ModelTypeProcessor implementation.
  void ConnectSync(std::unique_ptr<CommitQueue> worker) override;
  void DisconnectSync() override;
  void GetLocalChanges(size_t max_entries,
                       GetLocalChangesCallback callback) override;
  void OnCommitCompleted(
      const sync_pb::ModelTypeState& type_state,
      const CommitResponseDataList& committed_response_list,
      const FailedCommitResponseDataList& error_response_list) override;
  void OnCommitFailed(SyncCommitError commit_error) override;
  void OnUpdateReceived(const sync_pb::ModelTypeState& type_state,
                        UpdateResponseDataList updates) override;

  // ModelTypeControllerDelegate implementation.
  // |start_callback| will never be called synchronously.
  void OnSyncStarting(const DataTypeActivationRequest& request,
                      StartCallback callback) override;
  void OnSyncStopping(SyncStopMetadataFate metadata_fate) override;
  void GetAllNodesForDebugging(AllNodesCallback callback) override;
  void GetStatusCountersForDebugging(StatusCountersCallback callback) override;
  void RecordMemoryUsageAndCountsHistograms() override;

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

  bool HasLocalChangesForTest() const;

  bool IsTrackingEntityForTest(const std::string& storage_key) const;

  bool IsModelReadyToSyncForTest() const;

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused. Public for tests.
  enum class ErrorSite {
    kBridgeInitiated = 0,
    kApplyFullUpdates = 1,
    kApplyIncrementalUpdates = 2,
    kApplyUpdatesOnCommitResponse = 3,
    kSupportsIncrementalUpdatesMismatch = 4,
    kMaxValue = kSupportsIncrementalUpdatesMismatch,
  };

 private:
  friend class ModelTypeDebugInfo;
  friend class ClientTagBasedModelTypeProcessorTest;

  // Clears all metadata and directs the bridge to clear the persisted metadata
  // as well. In addition, it resets the state of the processor and clears all
  // tracking maps such as |entities_| and |storage_key_to_tag_hash_|.
  void ClearMetadataAndResetState();

  // Whether the processor is allowing changes to its model type. If this is
  // false, the bridge should not allow any changes to its data.
  bool IsAllowingChanges() const;

  // If preconditions are met, inform sync that we are ready to connect.
  void ConnectIfReady();

  // Validates the update specified by the input parameters and returns whether
  // it should get further processed. If the update is incorrect, this function
  // also reports an error.
  bool ValidateUpdate(const sync_pb::ModelTypeState& model_type_state,
                      const UpdateResponseDataList& updates);

  // Handle the first update received from the server after being enabled. If
  // the data type does not support incremental updates, this will be called for
  // any server update.
  base::Optional<ModelError> OnFullUpdateReceived(
      const sync_pb::ModelTypeState& type_state,
      UpdateResponseDataList updates);

  // Handle any incremental updates received from the server after being
  // enabled.
  base::Optional<ModelError> OnIncrementalUpdateReceived(
      const sync_pb::ModelTypeState& type_state,
      UpdateResponseDataList updates);

  // ModelTypeSyncBridge::GetData() callback for pending loading data upon
  // GetLocalChanges call.
  void OnPendingDataLoaded(size_t max_entries,
                           GetLocalChangesCallback callback,
                           std::unordered_set<std::string> storage_keys_to_load,
                           std::unique_ptr<DataBatch> data_batch);

  // Caches EntityData from the |data_batch| in the entity and checks
  // that every entity in |storage_keys_to_load| was successfully loaded (or is
  // not tracked by the processor any more). Reports failed checks to UMA.
  void ConsumeDataBatch(std::unordered_set<std::string> storage_keys_to_load,
                        std::unique_ptr<DataBatch> data_batch);

  // Prepares Commit requests and passes them to the GetLocalChanges callback.
  void CommitLocalChanges(size_t max_entries, GetLocalChangesCallback callback);

  // Nudges worker if there are any local entities to be committed.
  void NudgeForCommitIfNeeded();

  // Looks up the client tag hash for the given |storage_key|, and regenerates
  // with |data| if the lookup finds nothing. Does not update the storage key to
  // client tag hash mapping.
  ClientTagHash GetClientTagHash(const std::string& storage_key,
                                 const EntityData& data) const;

  // Create an entity in the entity map for |storage_key| and return a pointer
  // to it.
  // Requires that no entity for |storage_key| already exists in the map.
  // Never returns nullptr.
  ProcessorEntity* CreateEntity(const std::string& storage_key,
                                const EntityData& data);

  // Removes metadata for all entries unless they are unsynced.
  // This is used to limit the amount of data stored in sync, and this does not
  // tell the bridge to delete the actual data.
  void ExpireAllEntries(MetadataChangeList* metadata_changes);

  // Removes entity with specified |storage_key| and clears metadata for it from
  // |metadata_change_list|. |storage_key| must not be empty.
  void RemoveEntity(const std::string& storage_key,
                    MetadataChangeList* metadata_change_list);

  // Resets the internal state of the processor to the one right after calling
  // the ctor (with the exception of |bridge_| which remains intact).
  // TODO(jkrcal): Replace the helper function by grouping the state naturally
  // into a few structs / nested classes so that the state can be reset by
  // resetting these structs.
  void ResetState(SyncStopMetadataFate metadata_fate);

  // Adds metadata to all data returned by the bridge.
  // TODO(jkrcal): Mark as const (together with functions it depends on such as
  // GetEntityForStorageKey, GetEntityForTagHash and maybe more).
  void MergeDataWithMetadataForDebugging(AllNodesCallback callback,
                                         std::unique_ptr<DataBatch> batch);

  // Checks for valid cache GUID and data type id. Resets state if metadata is
  // invalid.
  void CheckForInvalidPersistedMetadata();

  // Reports error and records a metric about |site| where the error occurred.
  void ReportErrorImpl(const ModelError& error, ErrorSite site);

  /////////////////////
  // Processor state //
  /////////////////////

  // The model type this object syncs.
  const ModelType type_;

  // ModelTypeSyncBridge linked to this processor. The bridge owns this
  // processor instance so the pointer should never become invalid.
  ModelTypeSyncBridge* bridge_;

  // Function to capture and upload a stack trace when an error occurs.
  const base::RepeatingClosure dump_stack_;

  /////////////////
  // Model state //
  /////////////////

  // The first model error that occurred, if any. Stored to track model state
  // and so it can be passed to sync if it happened prior to sync being ready.
  base::Optional<ModelError> model_error_;

  // Whether the model has initialized its internal state for sync (and provided
  // metadata).
  bool model_ready_to_sync_ = false;

  ////////////////
  // Sync state //
  ////////////////

  // Stores the start callback in between OnSyncStarting() and ReadyToConnect().
  // |start_callback_| will never be called synchronously.
  StartCallback start_callback_;

  // The request context passed in as part of OnSyncStarting().
  DataTypeActivationRequest activation_request_;

  // Reference to the CommitQueue.
  //
  // The interface hides the posting of tasks across threads as well as the
  // CommitQueue's implementation.  Both of these features are
  // useful in tests.
  std::unique_ptr<CommitQueue> worker_;

  //////////////////
  // Entity state //
  //////////////////

  std::unique_ptr<ProcessorEntityTracker> entity_tracker_;

  // If the processor should behave as if |type_| is one of the commit only
  // model types. For this processor, being commit only means that on commit
  // confirmation, we should delete local data, because the model side never
  // intends to read it. This includes both data and metadata.
  const bool commit_only_;

  SEQUENCE_CHECKER(sequence_checker_);

  // WeakPtrFactory for this processor for ModelTypeController (only gets
  // invalidated during destruction).
  base::WeakPtrFactory<ModelTypeControllerDelegate>
      weak_ptr_factory_for_controller_{this};

  // WeakPtrFactory for this processor which will be sent to sync thread.
  base::WeakPtrFactory<ClientTagBasedModelTypeProcessor>
      weak_ptr_factory_for_worker_{this};

  DISALLOW_COPY_AND_ASSIGN(ClientTagBasedModelTypeProcessor);
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_MODEL_IMPL_CLIENT_TAG_BASED_MODEL_TYPE_PROCESSOR_H_
