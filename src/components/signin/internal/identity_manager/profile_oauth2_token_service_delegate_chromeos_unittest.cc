// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/internal/identity_manager/profile_oauth2_token_service_delegate_chromeos.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/stl_util.h"
#include "base/test/task_environment.h"
#include "chromeos/components/account_manager/account_manager.h"
#include "components/signin/internal/identity_manager/account_tracker_service.h"
#include "components/signin/internal/identity_manager/profile_oauth2_token_service_observer.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/test_signin_client.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "google_apis/gaia/gaia_urls.h"
#include "google_apis/gaia/oauth2_access_token_consumer.h"
#include "google_apis/gaia/oauth2_access_token_fetcher.h"
#include "google_apis/gaia/oauth2_access_token_manager_test_util.h"
#include "services/network/test/test_network_connection_tracker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace signin {

namespace {

using chromeos::account_manager::AccountType::ACCOUNT_TYPE_ACTIVE_DIRECTORY;
using chromeos::account_manager::AccountType::ACCOUNT_TYPE_GAIA;

constexpr char kGaiaId[] = "gaia-id";
constexpr char kGaiaToken[] = "gaia-token";
constexpr char kUserEmail[] = "user@gmail.com";

class AccessTokenConsumer : public OAuth2AccessTokenConsumer {
 public:
  AccessTokenConsumer() = default;
  ~AccessTokenConsumer() override = default;

  void OnGetTokenSuccess(const TokenResponse& token_response) override {
    ++num_access_token_fetch_success_;
  }

  void OnGetTokenFailure(const GoogleServiceAuthError& error) override {
    ++num_access_token_fetch_failure_;
  }

  int num_access_token_fetch_success_ = 0;
  int num_access_token_fetch_failure_ = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(AccessTokenConsumer);
};

class TestOAuth2TokenServiceObserver
    : public ProfileOAuth2TokenServiceObserver {
 public:
  // |delegate| is a non-owning pointer to an
  // |ProfileOAuth2TokenServiceDelegate| that MUST outlive |this| instance.
  explicit TestOAuth2TokenServiceObserver(
      ProfileOAuth2TokenServiceDelegate* delegate)
      : delegate_(delegate) {
    delegate_->AddObserver(this);
  }

  ~TestOAuth2TokenServiceObserver() override {
    delegate_->RemoveObserver(this);
  }

  void StartBatchChanges() {
    EXPECT_FALSE(is_inside_batch_);
    is_inside_batch_ = true;

    // Start a new batch
    batch_change_records_.emplace_back(std::vector<CoreAccountId>());
  }

  void OnEndBatchChanges() override {
    EXPECT_TRUE(is_inside_batch_);
    is_inside_batch_ = false;
  }

  void OnRefreshTokenAvailable(const CoreAccountId& account_id) override {
    if (!is_inside_batch_)
      StartBatchChanges();

    // We should not be seeing any cached errors for a freshly updated account,
    // except when they have been generated by us (i.e.
    // CREDENTIALS_REJECTED_BY_CLIENT).
    const GoogleServiceAuthError error = delegate_->GetAuthError(account_id);
    EXPECT_TRUE((error == GoogleServiceAuthError::AuthErrorNone()) ||
                (error.state() ==
                     GoogleServiceAuthError::State::INVALID_GAIA_CREDENTIALS &&
                 error.GetInvalidGaiaCredentialsReason() ==
                     GoogleServiceAuthError::InvalidGaiaCredentialsReason::
                         CREDENTIALS_REJECTED_BY_CLIENT));

    account_ids_.insert(account_id);

    // Record the |account_id| in the last batch.
    batch_change_records_.rbegin()->emplace_back(account_id);
  }

  void OnRefreshTokensLoaded() override { refresh_tokens_loaded_ = true; }

  void OnRefreshTokenRevoked(const CoreAccountId& account_id) override {
    if (!is_inside_batch_)
      StartBatchChanges();

    account_ids_.erase(account_id);
    // Record the |account_id| in the last batch.
    batch_change_records_.rbegin()->emplace_back(account_id);
  }

  void OnAuthErrorChanged(const CoreAccountId& account_id,
                          const GoogleServiceAuthError& auth_error) override {
    last_err_account_id_ = account_id;
    last_err_ = auth_error;
    on_auth_error_changed_calls++;
  }

  int on_auth_error_changed_calls = 0;

  CoreAccountId last_err_account_id_;
  GoogleServiceAuthError last_err_;
  std::set<CoreAccountId> account_ids_;
  bool is_inside_batch_ = false;
  bool refresh_tokens_loaded_ = false;

  // Records batch changes for later verification. Each index of this vector
  // represents a batch change. Each batch change is a vector of account ids for
  // which |OnRefreshTokenAvailable| is called.
  std::vector<std::vector<CoreAccountId>> batch_change_records_;

  // Non-owning pointer.
  ProfileOAuth2TokenServiceDelegate* const delegate_;
};

}  // namespace

class ProfileOAuth2TokenServiceDelegateChromeOSTest : public testing::Test {
 public:
  ProfileOAuth2TokenServiceDelegateChromeOSTest() {}
  ~ProfileOAuth2TokenServiceDelegateChromeOSTest() override = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
    AccountTrackerService::RegisterPrefs(pref_service_.registry());
    chromeos::AccountManager::RegisterPrefs(pref_service_.registry());

    client_ = std::make_unique<TestSigninClient>(&pref_service_);
    account_manager_.Initialize(tmp_dir_.GetPath(),
                                client_->GetURLLoaderFactory(),
                                immediate_callback_runner_);
    account_manager_.SetPrefService(&pref_service_);
    task_environment_.RunUntilIdle();

    account_tracker_service_.Initialize(&pref_service_, base::FilePath());

    account_info_ = CreateAccountInfoTestFixture(kGaiaId, kUserEmail);
    account_tracker_service_.SeedAccountInfo(account_info_);
    gaia_account_key_ = {account_info_.gaia, ACCOUNT_TYPE_GAIA};
    ad_account_key_ = {"object-guid", ACCOUNT_TYPE_ACTIVE_DIRECTORY};

    delegate_ = std::make_unique<ProfileOAuth2TokenServiceDelegateChromeOS>(
        &account_tracker_service_,
        network::TestNetworkConnectionTracker::GetInstance(), &account_manager_,
        true /* is_regular_profile */);
    delegate_->LoadCredentials(
        account_info_.account_id /* primary_account_id */);
  }

  AccountInfo CreateAccountInfoTestFixture(const std::string& gaia_id,
                                           const std::string& email) {
    AccountInfo account_info;

    account_info.gaia = gaia_id;
    account_info.email = email;
    account_info.full_name = "name";
    account_info.given_name = "name";
    account_info.hosted_domain = "example.com";
    account_info.locale = "en";
    account_info.picture_url = "https://example.com";
    account_info.is_child_account = false;
    account_info.account_id = account_tracker_service_.PickAccountIdForAccount(
        account_info.gaia, account_info.email);

    // Cannot use |ASSERT_TRUE| due to a |void| return type in an |ASSERT_TRUE|
    // branch.
    EXPECT_TRUE(account_info.IsValid());

    return account_info;
  }

  void AddSuccessfulOAuthTokenResponse() {
    client_->GetTestURLLoaderFactory()->AddResponse(
        GaiaUrls::GetInstance()->oauth2_token_url().spec(),
        GetValidTokenResponse("token", 3600));
  }

  base::test::TaskEnvironment task_environment_;

  base::ScopedTempDir tmp_dir_;
  AccountInfo account_info_;
  chromeos::AccountManager::AccountKey gaia_account_key_;
  chromeos::AccountManager::AccountKey ad_account_key_;
  AccountTrackerService account_tracker_service_;
  chromeos::AccountManager account_manager_;
  std::unique_ptr<ProfileOAuth2TokenServiceDelegateChromeOS> delegate_;
  chromeos::AccountManager::DelayNetworkCallRunner immediate_callback_runner_ =
      base::BindRepeating(
          [](base::OnceClosure closure) -> void { std::move(closure).Run(); });
  sync_preferences::TestingPrefServiceSyncable pref_service_;
  std::unique_ptr<TestSigninClient> client_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProfileOAuth2TokenServiceDelegateChromeOSTest);
};

// Refresh tokens should load successfully for non-regular (Signin and Lock
// Screen) Profiles.
TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokensAreLoadedForNonRegularProfiles) {
  // Create an instance of Account Manager but do not
  // |AccountManager::Initialize| it. This mimics Signin and Lock Screen Profile
  // behaviour.
  chromeos::AccountManager account_manager;

  auto delegate = std::make_unique<ProfileOAuth2TokenServiceDelegateChromeOS>(
      &account_tracker_service_,
      network::TestNetworkConnectionTracker::GetInstance(), &account_manager,
      false /* is_regular_profile */);
  TestOAuth2TokenServiceObserver observer(delegate.get());

  // Test that LoadCredentials works as expected.
  EXPECT_FALSE(observer.refresh_tokens_loaded_);
  delegate->LoadCredentials(CoreAccountId() /* primary_account_id */);
  EXPECT_TRUE(observer.refresh_tokens_loaded_);
  EXPECT_EQ(LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
            delegate->load_credentials_state());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokenIsAvailableReturnsTrueForValidGaiaTokens) {
  EXPECT_EQ(LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
            delegate_->load_credentials_state());

  EXPECT_FALSE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_FALSE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));

  account_manager_.UpsertAccount(gaia_account_key_, kUserEmail, kGaiaToken);

  EXPECT_TRUE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_TRUE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokenIsAvailableReturnsTrueForInvalidGaiaTokens) {
  EXPECT_EQ(LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
            delegate_->load_credentials_state());

  EXPECT_FALSE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_FALSE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));

  account_manager_.UpsertAccount(gaia_account_key_, kUserEmail,
                                 chromeos::AccountManager::kInvalidToken);

  EXPECT_TRUE(delegate_->RefreshTokenIsAvailable(account_info_.account_id));
  EXPECT_TRUE(
      base::Contains(delegate_->GetAccounts(), account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnAuthErrorChange) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);

  delegate_->UpdateAuthError(account_info_.account_id, error);
  EXPECT_EQ(error, delegate_->GetAuthError(account_info_.account_id));
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
  EXPECT_EQ(error, observer.last_err_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotNotifiedIfErrorDidntChange) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);

  delegate_->UpdateAuthError(account_info_.account_id, error);
  EXPECT_EQ(1, observer.on_auth_error_changed_calls);
  delegate_->UpdateAuthError(account_info_.account_id, error);
  EXPECT_EQ(1, observer.on_auth_error_changed_calls);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedIfErrorDidChange) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  delegate_->UpdateAuthError(
      account_info_.account_id,
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR));
  EXPECT_EQ(1, observer.on_auth_error_changed_calls);

  delegate_->UpdateAuthError(
      account_info_.account_id,
      GoogleServiceAuthError(
          GoogleServiceAuthError::State::INVALID_GAIA_CREDENTIALS));
  EXPECT_EQ(2, observer.on_auth_error_changed_calls);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnCredentialsInsertion) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);

  EXPECT_EQ(1UL, observer.account_ids_.size());
  EXPECT_EQ(account_info_.account_id, *observer.account_ids_.begin());
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(), observer.last_err_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversDoNotSeeCachedErrorsOnCredentialsUpdate) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);
  // Deliberately add an error.
  delegate_->UpdateAuthError(account_info_.account_id, error);

  // Update credentials. The delegate will check if see cached errors.
  delegate_->UpdateCredentials(account_info_.account_id, "new-token");
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       DummyTokensArePreEmptivelyRejected) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  delegate_->UpdateCredentials(account_info_.account_id,
                               chromeos::AccountManager::kInvalidToken);

  const GoogleServiceAuthError error =
      delegate_->GetAuthError(account_info_.account_id);
  EXPECT_EQ(GoogleServiceAuthError::State::INVALID_GAIA_CREDENTIALS,
            error.state());
  EXPECT_EQ(GoogleServiceAuthError::InvalidGaiaCredentialsReason::
                CREDENTIALS_REJECTED_BY_CLIENT,
            error.GetInvalidGaiaCredentialsReason());

  // Observer notification should also have notified about the same error.
  EXPECT_EQ(error, observer.last_err_);
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnCredentialsUpdate) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);

  EXPECT_EQ(1UL, observer.account_ids_.size());
  EXPECT_EQ(account_info_.account_id, *observer.account_ids_.begin());
  EXPECT_EQ(account_info_.account_id, observer.last_err_account_id_);
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(), observer.last_err_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotNotifiedIfCredentialsAreNotUpdated) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());

  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);
  observer.account_ids_.clear();
  observer.last_err_account_id_ = CoreAccountId();
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);

  EXPECT_TRUE(observer.account_ids_.empty());
  EXPECT_TRUE(observer.last_err_account_id_.empty());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BatchChangeObserversAreNotifiedOnCredentialsUpdate) {
  TestOAuth2TokenServiceObserver observer(delegate_.get());
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);

  EXPECT_EQ(1UL, observer.batch_change_records_.size());
  EXPECT_EQ(1UL, observer.batch_change_records_[0].size());
  EXPECT_EQ(account_info_.account_id, observer.batch_change_records_[0][0]);
}

// If observers register themselves with |ProfileOAuth2TokenServiceDelegate|
// before |chromeos::AccountManager| has been initialized, they should receive
// all the accounts stored in |chromeos::AccountManager| in a single batch.
TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BatchChangeObserversAreNotifiedOncePerBatch) {
  // Setup
  AccountInfo account1 = CreateAccountInfoTestFixture(
      "1" /* gaia_id */, "user1@example.com" /* email */);
  AccountInfo account2 = CreateAccountInfoTestFixture(
      "2" /* gaia_id */, "user2@example.com" /* email */);

  account_tracker_service_.SeedAccountInfo(account1);
  account_tracker_service_.SeedAccountInfo(account2);
  account_manager_.UpsertAccount(
      chromeos::AccountManager::AccountKey{account1.gaia, ACCOUNT_TYPE_GAIA},
      "user1@example.com", "token1");
  account_manager_.UpsertAccount(
      chromeos::AccountManager::AccountKey{account2.gaia, ACCOUNT_TYPE_GAIA},
      "user2@example.com", "token2");
  task_environment_.RunUntilIdle();

  chromeos::AccountManager account_manager;
  // chromeos::AccountManager will not be fully initialized until
  // |task_environment_.RunUntilIdle()| is called.
  account_manager.Initialize(tmp_dir_.GetPath(), client_->GetURLLoaderFactory(),
                             immediate_callback_runner_);
  account_manager.SetPrefService(&pref_service_);

  // Register callbacks before chromeos::AccountManager has been fully
  // initialized.
  auto delegate = std::make_unique<ProfileOAuth2TokenServiceDelegateChromeOS>(
      &account_tracker_service_,
      network::TestNetworkConnectionTracker::GetInstance(), &account_manager,
      true /* is_regular_profile */);
  delegate->LoadCredentials(account1.account_id /* primary_account_id */);
  TestOAuth2TokenServiceObserver observer(delegate.get());
  // Wait until chromeos::AccountManager is fully initialized.
  task_environment_.RunUntilIdle();

  // Tests

  // The observer should receive 3 batch change callbacks:
  // First - A batch of all accounts stored in chromeos::AccountManager: because
  // of the delegate's invocation of |chromeos::AccountManager::GetAccounts| in
  // its constructor. Followed by 2 updates for the individual accounts
  // (|account1| and |account2|): because of the delegate's registration as an
  // |chromeos::AccountManager::Observer| before |chromeos::AccountManager| has
  // been fully initialized.
  EXPECT_EQ(3UL, observer.batch_change_records_.size());

  const std::vector<CoreAccountId>& first_batch =
      observer.batch_change_records_[0];
  EXPECT_EQ(2UL, first_batch.size());
  EXPECT_TRUE(base::Contains(first_batch, account1.account_id));
  EXPECT_TRUE(base::Contains(first_batch, account2.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       GetAccountsShouldNotReturnAdAccounts) {
  EXPECT_TRUE(delegate_->GetAccounts().empty());

  // Insert an Active Directory account into chromeos::AccountManager.
  account_manager_.UpsertAccount(
      ad_account_key_, kUserEmail,
      chromeos::AccountManager::kActiveDirectoryDummyToken);

  // OAuth delegate should not return Active Directory accounts.
  EXPECT_TRUE(delegate_->GetAccounts().empty());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       GetAccountsReturnsGaiaAccounts) {
  EXPECT_TRUE(delegate_->GetAccounts().empty());

  account_manager_.UpsertAccount(gaia_account_key_, kUserEmail, kGaiaToken);

  std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  EXPECT_EQ(1UL, accounts.size());
  EXPECT_EQ(account_info_.account_id, accounts[0]);
}

// |GetAccounts| should return all known Gaia accounts, whether or not they have
// a "valid" refresh token stored against them.
TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       GetAccountsReturnsGaiaAccountsWithInvalidTokens) {
  EXPECT_TRUE(delegate_->GetAccounts().empty());

  account_manager_.UpsertAccount(gaia_account_key_, kUserEmail,
                                 chromeos::AccountManager::kInvalidToken);

  std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  EXPECT_EQ(1UL, accounts.size());
  EXPECT_EQ(account_info_.account_id, accounts[0]);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       RefreshTokenMustBeAvailableForAllAccountsReturnedByGetAccounts) {
  EXPECT_EQ(LoadCredentialsState::LOAD_CREDENTIALS_FINISHED_WITH_SUCCESS,
            delegate_->load_credentials_state());
  EXPECT_TRUE(delegate_->GetAccounts().empty());
  const std::string kUserEmail2 = "random-email2@example.com";
  const std::string kUserEmail3 = "random-email3@example.com";

  // Insert 2 Gaia accounts and 1 Active Directory Account. Of the 2 Gaia
  // accounts, 1 has a valid refresh token and 1 has a dummy token.
  account_manager_.UpsertAccount(gaia_account_key_, kUserEmail, kGaiaToken);

  chromeos::AccountManager::AccountKey gaia_account_key2{"random-gaia-id",
                                                         ACCOUNT_TYPE_GAIA};
  account_tracker_service_.SeedAccountInfo(
      CreateAccountInfoTestFixture(gaia_account_key2.id, kUserEmail2));
  account_manager_.UpsertAccount(gaia_account_key2, kUserEmail2,
                                 chromeos::AccountManager::kInvalidToken);

  account_manager_.UpsertAccount(
      ad_account_key_, kUserEmail3,
      chromeos::AccountManager::kActiveDirectoryDummyToken);

  // Verify.
  const std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  // 2 Gaia accounts should be returned.
  EXPECT_EQ(2UL, accounts.size());
  // And |RefreshTokenIsAvailable| should return true for these accounts.
  for (const CoreAccountId& account : accounts) {
    EXPECT_TRUE(delegate_->RefreshTokenIsAvailable(account));
  }
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       UpdateCredentialsSucceeds) {
  EXPECT_TRUE(delegate_->GetAccounts().empty());

  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);

  std::vector<CoreAccountId> accounts = delegate_->GetAccounts();
  EXPECT_EQ(1UL, accounts.size());
  EXPECT_EQ(account_info_.account_id, accounts[0]);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       ObserversAreNotifiedOnAccountRemoval) {
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);

  TestOAuth2TokenServiceObserver observer(delegate_.get());
  account_manager_.RemoveAccount(gaia_account_key_);

  EXPECT_EQ(1UL, observer.batch_change_records_.size());
  EXPECT_EQ(1UL, observer.batch_change_records_[0].size());
  EXPECT_EQ(account_info_.account_id, observer.batch_change_records_[0][0]);
  EXPECT_TRUE(observer.account_ids_.empty());
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       SigninErrorObserversAreNotifiedOnAuthErrorChange) {
  auto error =
      GoogleServiceAuthError(GoogleServiceAuthError::State::SERVICE_ERROR);

  delegate_->UpdateAuthError(account_info_.account_id, error);

  EXPECT_EQ(error, delegate_->GetAuthError(account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       TransientErrorsAreNotShown) {
  auto transient_error = GoogleServiceAuthError(
      GoogleServiceAuthError::State::SERVICE_UNAVAILABLE);
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(),
            delegate_->GetAuthError(account_info_.account_id));

  delegate_->UpdateAuthError(account_info_.account_id, transient_error);

  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(),
            delegate_->GetAuthError(account_info_.account_id));
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BackOffIsTriggerredForTransientErrors) {
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);
  auto transient_error = GoogleServiceAuthError(
      GoogleServiceAuthError::State::SERVICE_UNAVAILABLE);
  delegate_->UpdateAuthError(account_info_.account_id, transient_error);
  // Add a dummy success response. The actual network call has not been made
  // yet.
  AddSuccessfulOAuthTokenResponse();

  // Transient error should repeat until backoff period expires.
  AccessTokenConsumer access_token_consumer;
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_failure_);
  std::vector<std::string> scopes{"scope"};
  std::unique_ptr<OAuth2AccessTokenFetcher> fetcher =
      delegate_->CreateAccessTokenFetcher(account_info_.account_id,
                                          delegate_->GetURLLoaderFactory(),
                                          &access_token_consumer);
  task_environment_.RunUntilIdle();
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
  // Expect a positive backoff time.
  EXPECT_GT(delegate_->backoff_entry_.GetTimeUntilRelease(), base::TimeDelta());

  // Pretend that backoff has expired and try again.
  delegate_->backoff_entry_.SetCustomReleaseTime(base::TimeTicks());
  fetcher = delegate_->CreateAccessTokenFetcher(
      account_info_.account_id, delegate_->GetURLLoaderFactory(),
      &access_token_consumer);
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
}

TEST_F(ProfileOAuth2TokenServiceDelegateChromeOSTest,
       BackOffIsResetOnNetworkChange) {
  delegate_->UpdateCredentials(account_info_.account_id, kGaiaToken);
  auto transient_error = GoogleServiceAuthError(
      GoogleServiceAuthError::State::SERVICE_UNAVAILABLE);
  delegate_->UpdateAuthError(account_info_.account_id, transient_error);
  // Add a dummy success response. The actual network call has not been made
  // yet.
  AddSuccessfulOAuthTokenResponse();

  // Transient error should repeat until backoff period expires.
  AccessTokenConsumer access_token_consumer;
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_failure_);
  std::vector<std::string> scopes{"scope"};
  std::unique_ptr<OAuth2AccessTokenFetcher> fetcher =
      delegate_->CreateAccessTokenFetcher(account_info_.account_id,
                                          delegate_->GetURLLoaderFactory(),
                                          &access_token_consumer);
  task_environment_.RunUntilIdle();
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
  // Expect a positive backoff time.
  EXPECT_GT(delegate_->backoff_entry_.GetTimeUntilRelease(), base::TimeDelta());

  // Notify of network change and ensure that request now runs.
  delegate_->OnConnectionChanged(
      network::mojom::ConnectionType::CONNECTION_WIFI);
  fetcher = delegate_->CreateAccessTokenFetcher(
      account_info_.account_id, delegate_->GetURLLoaderFactory(),
      &access_token_consumer);
  fetcher->Start("client_id", "client_secret", scopes);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_success_);
  EXPECT_EQ(1, access_token_consumer.num_access_token_fetch_failure_);
}

}  // namespace signin
