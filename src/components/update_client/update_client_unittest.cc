// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <tuple>
#include <utility>

#include "base/bind.h"
#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/optional.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/task/post_task.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/test/scoped_path_override.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "base/version.h"
#include "build/build_config.h"
#include "components/crx_file/crx_verifier.h"
#include "components/prefs/testing_pref_service.h"
#include "components/update_client/component_unpacker.h"
#include "components/update_client/crx_update_item.h"
#include "components/update_client/network.h"
#include "components/update_client/patcher.h"
#include "components/update_client/persisted_data.h"
#include "components/update_client/ping_manager.h"
#include "components/update_client/protocol_handler.h"
#include "components/update_client/test_configurator.h"
#include "components/update_client/test_installer.h"
#include "components/update_client/unzipper.h"
#include "components/update_client/update_checker.h"
#include "components/update_client/update_client_errors.h"
#include "components/update_client/update_client_internal.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace update_client {

namespace {

using base::FilePath;

// Makes a copy of the file specified by |from_path| in a temporary directory
// and returns the path of the copy. Returns true if successful. Cleans up if
// there was an error creating the copy.
bool MakeTestFile(const FilePath& from_path, FilePath* to_path) {
  FilePath temp_dir;
  bool result =
      CreateNewTempDirectory(FILE_PATH_LITERAL("update_client"), &temp_dir);
  if (!result)
    return false;

  FilePath temp_file;
  result = CreateTemporaryFileInDir(temp_dir, &temp_file);
  if (!result)
    return false;

  result = CopyFile(from_path, temp_file);
  if (!result) {
    DeleteFile(temp_file, false);
    return false;
  }

  *to_path = temp_file;
  return true;
}

using Events = UpdateClient::Observer::Events;

class MockObserver : public UpdateClient::Observer {
 public:
  MOCK_METHOD2(OnEvent, void(Events event, const std::string&));
};

class MockActionHandler : public ActionHandler {
 public:
  MockActionHandler() = default;
  MockActionHandler(const MockActionHandler&) = delete;
  MockActionHandler& operator=(const MockActionHandler&) = delete;

  MOCK_METHOD3(Handle,
               void(const base::FilePath&, const std::string&, Callback));

 private:
  ~MockActionHandler() override = default;
};

class MockCrxStateChangeReceiver
    : public base::RefCounted<MockCrxStateChangeReceiver> {
 public:
  MOCK_METHOD(void, Receive, (CrxUpdateItem));

 private:
  friend class base::RefCounted<MockCrxStateChangeReceiver>;

  ~MockCrxStateChangeReceiver() = default;
};

}  // namespace

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::Return;
using ::testing::Unused;

using std::string;

class MockPingManagerImpl : public PingManager {
 public:
  struct PingData {
    std::string id;
    base::Version previous_version;
    base::Version next_version;
    ErrorCategory error_category = ErrorCategory::kNone;
    int error_code = 0;
    int extra_code1 = 0;
    ErrorCategory diff_error_category = ErrorCategory::kNone;
    int diff_error_code = 0;
    bool diff_update_failed = false;
  };

  explicit MockPingManagerImpl(scoped_refptr<Configurator> config);

  void SendPing(const Component& component, Callback callback) override;

  const std::vector<PingData>& ping_data() const;

  const std::vector<base::Value>& events() const;

 protected:
  ~MockPingManagerImpl() override;

 private:
  std::vector<PingData> ping_data_;
  std::vector<base::Value> events_;
  DISALLOW_COPY_AND_ASSIGN(MockPingManagerImpl);
};

MockPingManagerImpl::MockPingManagerImpl(scoped_refptr<Configurator> config)
    : PingManager(config) {}

MockPingManagerImpl::~MockPingManagerImpl() = default;

void MockPingManagerImpl::SendPing(const Component& component,
                                   Callback callback) {
  PingData ping_data;
  ping_data.id = component.id_;
  ping_data.previous_version = component.previous_version_;
  ping_data.next_version = component.next_version_;
  ping_data.error_category = component.error_category_;
  ping_data.error_code = component.error_code_;
  ping_data.extra_code1 = component.extra_code1_;
  ping_data.diff_error_category = component.diff_error_category_;
  ping_data.diff_error_code = component.diff_error_code_;
  ping_data.diff_update_failed = component.diff_update_failed();
  ping_data_.push_back(ping_data);

  events_ = component.GetEvents();

  std::move(callback).Run(0, "");
}

const std::vector<MockPingManagerImpl::PingData>&
MockPingManagerImpl::ping_data() const {
  return ping_data_;
}

const std::vector<base::Value>& MockPingManagerImpl::events() const {
  return events_;
}

class UpdateClientTest : public testing::Test {
 public:
  UpdateClientTest();
  ~UpdateClientTest() override;

 protected:
  void RunThreads();

  // Returns the full path to a test file.
  static base::FilePath TestFilePath(const char* file);

  scoped_refptr<update_client::TestConfigurator> config() { return config_; }
  update_client::PersistedData* metadata() { return metadata_.get(); }

  base::OnceClosure quit_closure() { return runloop_.QuitClosure(); }

 private:
  static constexpr int kNumWorkerThreads_ = 2;

  base::test::TaskEnvironment task_environment_;
  base::RunLoop runloop_;

  std::unique_ptr<TestingPrefServiceSimple> pref_ =
      std::make_unique<TestingPrefServiceSimple>();
  scoped_refptr<update_client::TestConfigurator> config_ =
      base::MakeRefCounted<TestConfigurator>(pref_.get());
  std::unique_ptr<update_client::PersistedData> metadata_ =
      std::make_unique<PersistedData>(pref_.get(), nullptr);

  DISALLOW_COPY_AND_ASSIGN(UpdateClientTest);
};

constexpr int UpdateClientTest::kNumWorkerThreads_;

UpdateClientTest::UpdateClientTest() {
  PersistedData::RegisterPrefs(pref_->registry());
}

UpdateClientTest::~UpdateClientTest() = default;

void UpdateClientTest::RunThreads() {
  runloop_.Run();
  task_environment_.RunUntilIdle();
}

base::FilePath UpdateClientTest::TestFilePath(const char* file) {
  base::FilePath path;
  base::PathService::Get(base::DIR_SOURCE_ROOT, &path);
  return path.AppendASCII("components")
      .AppendASCII("test")
      .AppendASCII("data")
      .AppendASCII("update_client")
      .AppendASCII(file);
}

// Tests the scenario where one update check is done for one CRX. The CRX
// has no update.
TEST_F(UpdateClientTest, OneCrxNoUpdate) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      std::vector<base::Optional<CrxComponent>> component = {crx};
      return component;
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      auto& component = components.at(id);

      EXPECT_TRUE(component->is_foreground());

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "noupdate";

      ProtocolParser::Results results;
      results.list.push_back(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver), true,
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(2u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kUpToDate, items[1].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[1].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the scenario where two CRXs are checked for updates. On CRX has
// an update, the other CRX does not.
TEST_F(UpdateClientTest, TwoCrxUpdateNoUpdate) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeRefCounted<TestInstaller>();
      crx1.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      CrxComponent crx2;
      crx2.name = "test_abag";
      crx2.pk_hash.assign(abag_hash, abag_hash + base::size(abag_hash));
      crx2.version = base::Version("2.2");
      crx2.installer = base::MakeRefCounted<TestInstaller>();
      crx2.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      return {crx1, crx2};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='7ab32f071cd9b5ef8e0d7913be161f532d98b3e9f
                                      a284a7cd8059c3409ce0498'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
        <app appid='abagagagagagagagagagagagagagagag'>
          <updatecheck status='noupdate'/>
        </app>
      </response>
      */
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(2u, ids_to_check.size());

      ProtocolParser::Results results;
      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "7ab32f071cd9b5ef8e0d7913be161f532d98b3e9fa284a7cd8059c3409ce0498";

        ProtocolParser::Result result;
        result.extension_id = "jebgalgnebhfojomionfpkfelancnnkf";
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);

        EXPECT_FALSE(components.at(id)->is_foreground());
      }

      {
        const std::string id = "abagagagagagagagagagagagagagagag";
        EXPECT_EQ(id, ids_to_check[1]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "noupdate";
        results.list.push_back(result);

        EXPECT_FALSE(components.at(id)->is_foreground());
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      download_metrics.url = url;
      download_metrics.downloader = DownloadMetrics::kNone;
      download_metrics.error = 0;
      download_metrics.downloaded_bytes = 1843;
      download_metrics.total_bytes = 1843;
      download_metrics.download_time_ms = 1000;

      FilePath path;
      EXPECT_TRUE(MakeTestFile(
          TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

      Result result;
      result.error = 0;
      result.response = path;

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes / 2,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(0, ping_data[0].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "abagagagagagagagagagagagagagagag")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "abagagagagagagagagagagagagagagag")).Times(1);
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "abagagagagagagagagagagagagagagag"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(9u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kChecking, items[1].state);
  EXPECT_STREQ("abagagagagagagagagagagagagagagag", items[1].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[2].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[2].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[3].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[3].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[4].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[4].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[5].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[5].id.c_str());
  EXPECT_EQ(ComponentState::kUpdating, items[6].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[6].id.c_str());
  EXPECT_EQ(ComponentState::kUpdated, items[7].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[7].id.c_str());
  EXPECT_EQ(ComponentState::kUpToDate, items[8].state);
  EXPECT_STREQ("abagagagagagagagagagagagagagagag", items[8].id.c_str());

  std::vector<std::tuple<int64_t, int64_t>> progress_bytes = {
      {-1, -1},     {-1, -1},     {-1, -1},     {-1, -1}, {921, 1843},
      {1843, 1843}, {1843, 1843}, {1843, 1843}, {-1, -1}};
  EXPECT_EQ(items.size(), progress_bytes.size());
  for (size_t i{0}; i != items.size(); ++i) {
    EXPECT_EQ(items[i].downloaded_bytes, std::get<0>(progress_bytes[i]));
    EXPECT_EQ(items[i].total_bytes, std::get<1>(progress_bytes[i]));
  }

  update_client->RemoveObserver(&observer);
}

// Tests the scenario where two CRXs are checked for updates. One CRX has
// an update but the server ignores the second CRX and returns no response for
// it. The second component gets an |UPDATE_RESPONSE_NOT_FOUND| error and
// transitions to the error state.
TEST_F(UpdateClientTest, TwoCrxUpdateFirstServerIgnoresSecond) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeRefCounted<TestInstaller>();
      crx1.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      CrxComponent crx2;
      crx2.name = "test_abag";
      crx2.pk_hash.assign(abag_hash, abag_hash + base::size(abag_hash));
      crx2.version = base::Version("2.2");
      crx2.installer = base::MakeRefCounted<TestInstaller>();
      crx2.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      return {crx1, crx2};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='7ab32f071cd9b5ef8e0d7913be161f532d98b3e9f
                                      a284a7cd8059c3409ce0498'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(2u, ids_to_check.size());

      ProtocolParser::Results results;
      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "7ab32f071cd9b5ef8e0d7913be161f532d98b3e9fa284a7cd8059c3409ce0498";

        ProtocolParser::Result result;
        result.extension_id = "jebgalgnebhfojomionfpkfelancnnkf";
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);

        EXPECT_FALSE(components.at(id)->is_foreground());
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      download_metrics.url = url;
      download_metrics.downloader = DownloadMetrics::kNone;
      download_metrics.error = 0;
      download_metrics.downloaded_bytes = 1843;
      download_metrics.total_bytes = 1843;
      download_metrics.download_time_ms = 1000;

      FilePath path;
      EXPECT_TRUE(MakeTestFile(
          TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

      Result result;
      result.error = 0;
      result.response = path;

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(0, ping_data[0].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "abagagagagagagagagagagagagagagag"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "abagagagagagagagagagagagagagagag"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_EQ(5, static_cast<int>(item.error_category));
          EXPECT_EQ(-10004, item.error_code);
          EXPECT_EQ(0, item.extra_code1);
        }));
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "abagagagagagagagagagagagagagagag"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(8u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kChecking, items[1].state);
  EXPECT_STREQ("abagagagagagagagagagagagagagagag", items[1].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[2].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[2].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[3].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[3].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[4].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[4].id.c_str());
  EXPECT_EQ(ComponentState::kUpdating, items[5].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[5].id.c_str());
  EXPECT_EQ(ComponentState::kUpdated, items[6].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[6].id.c_str());
  EXPECT_EQ(ComponentState::kUpdateError, items[7].state);
  EXPECT_STREQ("abagagagagagagagagagagagagagagag", items[7].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the update check for two CRXs scenario when the second CRX does not
// provide a CrxComponent instance. In this case, the update is handled as
// if only one component were provided as an argument to the |Update| call
// with the exception that the second component still fires an event such as
// |COMPONENT_UPDATE_ERROR|.
TEST_F(UpdateClientTest, TwoCrxUpdateNoCrxComponentData) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      return {crx, base::nullopt};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='7ab32f071cd9b5ef8e0d7913be161f532d98b3e9f
                                      a284a7cd8059c3409ce0498'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());

      ProtocolParser::Results results;
      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "7ab32f071cd9b5ef8e0d7913be161f532d98b3e9fa284a7cd8059c3409ce0498";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);

        EXPECT_FALSE(components.at(id)->is_foreground());
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/jebgalgnebhfojomionfpkfelancnnkf.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 1843;
        download_metrics.total_bytes = 1843;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

        result.error = 0;
        result.response = path;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(0, ping_data[0].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "ihfokbkgjpifnbbojhneepfflplebdkc"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(7u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kUpdateError, items[1].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[2].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[2].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[3].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[3].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[4].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[4].id.c_str());
  EXPECT_EQ(ComponentState::kUpdating, items[5].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[5].id.c_str());
  EXPECT_EQ(ComponentState::kUpdated, items[6].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[6].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the update check for two CRXs scenario when no CrxComponent data is
// provided for either component. In this case, no update check occurs, and
// |COMPONENT_UPDATE_ERROR| event fires for both components.
TEST_F(UpdateClientTest, TwoCrxUpdateNoCrxComponentDataAtAll) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      return {base::nullopt, base::nullopt};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      NOTREACHED();
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { NOTREACHED(); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      EXPECT_EQ(0u, MockPingManagerImpl::ping_data().size());
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "ihfokbkgjpifnbbojhneepfflplebdkc"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(2u, items.size());
  EXPECT_EQ(ComponentState::kUpdateError, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kUpdateError, items[1].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the scenario where there is a download timeout for the first
// CRX. The update for the first CRX fails. The update client waits before
// attempting the update for the second CRX. This update succeeds.
TEST_F(UpdateClientTest, TwoCrxUpdateDownloadTimeout) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeRefCounted<TestInstaller>();
      crx1.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      CrxComponent crx2;
      crx2.name = "test_ihfo";
      crx2.pk_hash.assign(ihfo_hash, ihfo_hash + base::size(ihfo_hash));
      crx2.version = base::Version("0.8");
      crx2.installer = base::MakeRefCounted<TestInstaller>();
      crx2.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      return {crx1, crx2};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='7ab32f071cd9b5ef8e0d7913be161f532d98b3e9f
                                      a284a7cd8059c3409ce0498'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
        <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                         hash_sha256='8f5aa190311237cae00675af87ff457f278cd1a05
                                      895470ac5d46647d4a3c2ea'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */

      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(2u, ids_to_check.size());

      ProtocolParser::Results results;
      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "7ab32f071cd9b5ef8e0d7913be161f532d98b3e9fa284a7cd8059c3409ce0498";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      }

      {
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[1]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "8f5aa190311237cae00675af87ff457f278cd1a05895470ac5d46647d4a3c2ea";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/jebgalgnebhfojomionfpkfelancnnkf.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = -118;
        download_metrics.downloaded_bytes = 0;
        download_metrics.total_bytes = 0;
        download_metrics.download_time_ms = 1000;

        // The result must not include a file path in the case of errors.
        result.error = -118;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(1, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(-118, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[1].error_category));
      EXPECT_EQ(0, ping_data[1].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_EQ(1, static_cast<int>(item.error_category));
          EXPECT_EQ(-118, item.error_code);
          EXPECT_EQ(0, item.extra_code1);
        }));
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_WAIT,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "ihfokbkgjpifnbbojhneepfflplebdkc"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(11u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kChecking, items[1].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[2].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[2].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[3].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[3].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[4].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[4].id.c_str());
  EXPECT_EQ(ComponentState::kUpdateError, items[5].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[5].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[6].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[6].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[7].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[7].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[8].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[8].id.c_str());
  EXPECT_EQ(ComponentState::kUpdating, items[9].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[9].id.c_str());
  EXPECT_EQ(ComponentState::kUpdated, items[10].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[10].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the differential update scenario for one CRX. Tests install progress
// for differential and full updates.
TEST_F(UpdateClientTest, OneCrxDiffUpdate) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      static int num_calls = 0;

      // Must use the same stateful installer object.
      static auto installer = base::MakeRefCounted<VersionedTestInstaller>();
      installer->set_installer_progress_samples({-1, 50, 100});

      ++num_calls;

      CrxComponent crx;
      crx.name = "test_ihfo";
      crx.pk_hash.assign(ihfo_hash, ihfo_hash + base::size(ihfo_hash));
      crx.installer = installer;
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      if (num_calls == 1) {
        crx.version = base::Version("0.8");
      } else if (num_calls == 2) {
        crx.version = base::Version("1.0");
      } else {
        NOTREACHED();
      }

      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());

      static int num_call = 0;
      ++num_call;

      ProtocolParser::Results results;

      if (num_call == 1) {
        /*
        Mock the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.1'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
              </urls>
              <manifest version='1.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                           hash_sha256='813c59747e139a608b3b5fc49633affc6db57437
                                        3f309f156ea6d27229c0b3f9'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "8f5aa190311237cae00675af87ff457f278cd1a05895470ac5d46647d4a3c2ea";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      } else if (num_call == 2) {
        /*
        Mock the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.1'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
                <url codebasediff='http://localhost/download/'/>
              </urls>
              <manifest version='2.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_2.crx'
                           namediff='ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx'
                           hash_sha256='c87d8742c3ff3d7a0cb6f3c91aa2fcf3dea6361
                                        8086a7db1c5be5300e1d4d6b6'
                           fp='22'
                           hashdiff_sha256='0fd48a5dd87006a709756cfc47198cbc4c4
                                            928f33ac4277d79573c15164a33eb'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_2.crx";
        package.namediff = "ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx";
        package.hash_sha256 =
            "c87d8742c3ff3d7a0cb6f3c91aa2fcf3dea63618086a7db1c5be5300e1d4d6b6";
        package.hashdiff_sha256 =
            "0fd48a5dd87006a709756cfc47198cbc4c4928f33ac4277d79573c15164a33eb";
        package.fingerprint = "22";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.crx_diffurls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "2.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 2105;
        download_metrics.total_bytes = 2105;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx"), &path));

        result.error = 0;
        result.response = path;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes / 2,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[0].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(0, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("2.0"), ping_data[1].next_version);
      EXPECT_FALSE(ping_data[1].diff_update_failed);
      EXPECT_EQ(0, static_cast<int>(ping_data[1].diff_error_category));
      EXPECT_EQ(0, ping_data[1].diff_error_code);
      EXPECT_EQ(0, static_cast<int>(ping_data[1].error_category));
      EXPECT_EQ(0, ping_data[1].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_UPDATING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(3);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_UPDATING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(3);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
  }

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"ihfokbkgjpifnbbojhneepfflplebdkc"};
  {
    std::vector<CrxUpdateItem> items;
    auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
    EXPECT_CALL(*receiver, Receive(_))
        .WillRepeatedly(
            [&items](const CrxUpdateItem& item) { items.push_back(item); });

    base::RunLoop runloop;
    update_client->Update(
        ids, base::BindOnce(&DataCallbackMock::Callback),
        base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
        false,
        base::BindOnce(&CompletionCallbackMock::Callback,
                       runloop.QuitClosure()));
    runloop.Run();

    EXPECT_EQ(10u, items.size());
    EXPECT_EQ(ComponentState::kChecking, items[0].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[0].id.c_str());
    EXPECT_EQ(ComponentState::kCanUpdate, items[1].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());
    EXPECT_EQ(ComponentState::kDownloading, items[2].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[2].id.c_str());
    EXPECT_EQ(ComponentState::kDownloading, items[3].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[3].id.c_str());
    EXPECT_EQ(ComponentState::kDownloading, items[4].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[4].id.c_str());
    EXPECT_EQ(ComponentState::kUpdating, items[5].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[5].id.c_str());
    EXPECT_EQ(ComponentState::kUpdating, items[6].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[6].id.c_str());
    EXPECT_EQ(ComponentState::kUpdating, items[7].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[7].id.c_str());
    EXPECT_EQ(ComponentState::kUpdating, items[8].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[8].id.c_str());
    EXPECT_EQ(ComponentState::kUpdated, items[9].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[9].id.c_str());

    std::vector<int> samples = {-1, -1, -1, -1, -1, -1, -1, 50, 100, 100};
    EXPECT_EQ(items.size(), samples.size());
    for (size_t i = 0; i != items.size(); ++i)
      EXPECT_EQ(items[i].install_progress, samples[i]);
  }

  {
    std::vector<CrxUpdateItem> items;
    auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
    EXPECT_CALL(*receiver, Receive(_))
        .WillRepeatedly(
            [&items](const CrxUpdateItem& item) { items.push_back(item); });

    base::RunLoop runloop;
    update_client->Update(
        ids, base::BindOnce(&DataCallbackMock::Callback),
        base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
        false,
        base::BindOnce(&CompletionCallbackMock::Callback,
                       runloop.QuitClosure()));
    runloop.Run();

    EXPECT_EQ(10u, items.size());
    EXPECT_EQ(ComponentState::kChecking, items[0].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[0].id.c_str());
    EXPECT_EQ(ComponentState::kCanUpdate, items[1].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());
    EXPECT_EQ(ComponentState::kDownloadingDiff, items[2].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[2].id.c_str());
    EXPECT_EQ(ComponentState::kDownloadingDiff, items[3].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[3].id.c_str());
    EXPECT_EQ(ComponentState::kDownloadingDiff, items[4].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[4].id.c_str());
    EXPECT_EQ(ComponentState::kUpdatingDiff, items[5].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[5].id.c_str());
    EXPECT_EQ(ComponentState::kUpdatingDiff, items[6].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[6].id.c_str());
    EXPECT_EQ(ComponentState::kUpdatingDiff, items[7].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[7].id.c_str());
    EXPECT_EQ(ComponentState::kUpdatingDiff, items[8].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[8].id.c_str());
    EXPECT_EQ(ComponentState::kUpdated, items[9].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[9].id.c_str());

    std::vector<int> samples = {-1, -1, -1, -1, -1, -1, -1, 50, 100, 100};
    EXPECT_EQ(items.size(), samples.size());
    for (size_t i = 0; i != items.size(); ++i)
      EXPECT_EQ(items[i].install_progress, samples[i]);
  }

  update_client->RemoveObserver(&observer);
}

// Tests the update scenario for one CRX where the CRX installer returns
// an error. Tests that the |unpack_path| argument refers to a valid path
// then |Install| is called, then tests that the |unpack| path is deleted
// by the |update_client| code before the test ends.
TEST_F(UpdateClientTest, OneCrxInstallError) {
  class MockInstaller : public CrxInstaller {
   public:
    MOCK_METHOD1(OnUpdateError, void(int error));
    MOCK_METHOD2(DoInstall,
                 void(const base::FilePath& unpack_path,
                      const Callback& callback));
    MOCK_METHOD2(GetInstalledFile,
                 bool(const std::string& file, base::FilePath* installed_file));
    MOCK_METHOD0(Uninstall, bool());

    void Install(const base::FilePath& unpack_path,
                 const std::string& public_key,
                 std::unique_ptr<InstallParams> /*install_params*/,
                 ProgressCallback progress_callback,
                 Callback callback) override {
      DoInstall(unpack_path, std::move(callback));

      unpack_path_ = unpack_path;
      EXPECT_TRUE(base::DirectoryExists(unpack_path_));
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock()},
          base::BindOnce(std::move(callback),
                         CrxInstaller::Result(InstallError::GENERIC_ERROR)));
    }

   protected:
    ~MockInstaller() override {
      // The unpack path is deleted unconditionally by the component state code,
      // which is driving this installer. Therefore, the unpack path must not
      // exist when this object is destroyed.
      if (!unpack_path_.empty())
        EXPECT_FALSE(base::DirectoryExists(unpack_path_));
    }

   private:
    // Contains the |unpack_path| argument of the Install call.
    base::FilePath unpack_path_;
  };

  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      scoped_refptr<MockInstaller> installer =
          base::MakeRefCounted<MockInstaller>();

      EXPECT_CALL(*installer, OnUpdateError(_)).Times(0);
      EXPECT_CALL(*installer, DoInstall(_, _)).Times(1);
      EXPECT_CALL(*installer, GetInstalledFile(_, _)).Times(0);
      EXPECT_CALL(*installer, Uninstall()).Times(0);

      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = installer;
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='7ab32f071cd9b5ef8e0d7913be161f532d98b3e9f
                                      a284a7cd8059c3409ce0498'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_FALSE(session_id.empty());

      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check[0]);
      EXPECT_EQ(1u, components.count(id));

      ProtocolParser::Result::Manifest::Package package;
      package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
      package.hash_sha256 =
          "7ab32f071cd9b5ef8e0d7913be161f532d98b3e9fa284a7cd8059c3409ce0498";

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "ok";
      result.crx_urls.push_back(GURL("http://localhost/download/"));
      result.manifest.version = "1.0";
      result.manifest.browser_min_version = "11.0.1.0";
      result.manifest.packages.push_back(package);

      ProtocolParser::Results results;
      results.list.push_back(result);
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      download_metrics.url = url;
      download_metrics.downloader = DownloadMetrics::kNone;
      download_metrics.error = 0;
      download_metrics.downloaded_bytes = 1843;
      download_metrics.total_bytes = 1843;
      download_metrics.download_time_ms = 1000;

      FilePath path;
      EXPECT_TRUE(MakeTestFile(
          TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

      Result result;
      result.error = 0;
      result.response = path;

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(3, static_cast<int>(ping_data[0].error_category));  // kInstall.
      EXPECT_EQ(9, ping_data[0].error_code);  // kInstallerError.
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(6u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[1].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[1].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[2].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[2].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[3].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[3].id.c_str());
  EXPECT_EQ(ComponentState::kUpdating, items[4].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[4].id.c_str());
  EXPECT_EQ(ComponentState::kUpdateError, items[5].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[5].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the fallback from differential to full update scenario for one CRX.
TEST_F(UpdateClientTest, OneCrxDiffUpdateFailsFullUpdateSucceeds) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      static int num_calls = 0;

      // Must use the same stateful installer object.
      static auto installer = base::MakeRefCounted<VersionedTestInstaller>();

      ++num_calls;

      CrxComponent crx;
      crx.name = "test_ihfo";
      crx.pk_hash.assign(ihfo_hash, ihfo_hash + base::size(ihfo_hash));
      crx.installer = installer;
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      if (num_calls == 1) {
        crx.version = base::Version("0.8");
      } else if (num_calls == 2) {
        crx.version = base::Version("1.0");
      } else {
        NOTREACHED();
      }

      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());

      static int num_call = 0;
      ++num_call;

      ProtocolParser::Results results;

      if (num_call == 1) {
        /*
        Mock the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.1'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
              </urls>
              <manifest version='1.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                           hash_sha256='813c59747e139a608b3b5fc49633affc6db57437
                                        3f309f156ea6d27229c0b3f9'
                           fp='1'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "8f5aa190311237cae00675af87ff457f278cd1a05895470ac5d46647d4a3c2ea";
        package.fingerprint = "1";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      } else if (num_call == 2) {
        /*
        Mock the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.1'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
                <url codebasediff='http://localhost/download/'/>
              </urls>
              <manifest version='2.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_2.crx'
                           namediff='ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx'
                           hash_sha256='c87d8742c3ff3d7a0cb6f3c91aa2fcf3dea6361
                                        8086a7db1c5be5300e1d4d6b6'
                           fp='22'
                           hashdiff_sha256='0fd48a5dd87006a709756cfc47198cbc4c4
                                            928f33ac4277d79573c15164a33eb'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_2.crx";
        package.namediff = "ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx";
        package.hash_sha256 =
            "c87d8742c3ff3d7a0cb6f3c91aa2fcf3dea63618086a7db1c5be5300e1d4d6b6";
        package.hashdiff_sha256 =
            "0fd48a5dd87006a709756cfc47198cbc4c4928f33ac4277d79573c15164a33eb";
        package.fingerprint = "22";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.crx_diffurls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "2.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx") {
        // A download error is injected on this execution path.
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = -1;
        download_metrics.downloaded_bytes = 0;
        download_metrics.total_bytes = 2105;
        download_metrics.download_time_ms = 1000;

        // The response must not include a file path in the case of errors.
        result.error = -1;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_2.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53855;
        download_metrics.total_bytes = 53855;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_2.crx"), &path));

        result.error = 0;
        result.response = path;
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[0].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(0, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("2.0"), ping_data[1].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[1].error_category));
      EXPECT_EQ(0, ping_data[1].error_code);
      EXPECT_TRUE(ping_data[1].diff_update_failed);
      EXPECT_EQ(1, static_cast<int>(ping_data[1].diff_error_category));
      EXPECT_EQ(-1, ping_data[1].diff_error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);

    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"ihfokbkgjpifnbbojhneepfflplebdkc"};

  {
    std::vector<CrxUpdateItem> items;
    auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
    EXPECT_CALL(*receiver, Receive(_))
        .WillRepeatedly(
            [&items](const CrxUpdateItem& item) { items.push_back(item); });

    base::RunLoop runloop;
    update_client->Update(
        ids, base::BindOnce(&DataCallbackMock::Callback),
        base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
        false,
        base::BindOnce(&CompletionCallbackMock::Callback,
                       runloop.QuitClosure()));
    runloop.Run();
    EXPECT_EQ(6u, items.size());
    EXPECT_EQ(ComponentState::kChecking, items[0].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[0].id.c_str());
    EXPECT_EQ(ComponentState::kCanUpdate, items[1].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());
    EXPECT_EQ(ComponentState::kDownloading, items[2].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[2].id.c_str());
    EXPECT_EQ(ComponentState::kDownloading, items[3].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[3].id.c_str());
    EXPECT_EQ(ComponentState::kUpdating, items[4].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[4].id.c_str());
    EXPECT_EQ(ComponentState::kUpdated, items[5].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[5].id.c_str());
  }

  {
    std::vector<CrxUpdateItem> items;
    auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
    EXPECT_CALL(*receiver, Receive(_))
        .WillRepeatedly(
            [&items](const CrxUpdateItem& item) { items.push_back(item); });

    base::RunLoop runloop;
    update_client->Update(
        ids, base::BindOnce(&DataCallbackMock::Callback),
        base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
        false,
        base::BindOnce(&CompletionCallbackMock::Callback,
                       runloop.QuitClosure()));
    runloop.Run();

    EXPECT_EQ(8u, items.size());
    EXPECT_EQ(ComponentState::kChecking, items[0].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[0].id.c_str());
    EXPECT_EQ(ComponentState::kCanUpdate, items[1].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());
    EXPECT_EQ(ComponentState::kDownloadingDiff, items[2].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[2].id.c_str());
    EXPECT_EQ(ComponentState::kDownloadingDiff, items[3].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[3].id.c_str());
    EXPECT_EQ(ComponentState::kDownloading, items[4].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[4].id.c_str());
    EXPECT_EQ(ComponentState::kDownloading, items[5].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[5].id.c_str());
    EXPECT_EQ(ComponentState::kUpdating, items[6].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[6].id.c_str());
    EXPECT_EQ(ComponentState::kUpdated, items[7].state);
    EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[7].id.c_str());
  }

  update_client->RemoveObserver(&observer);
}

// Tests the queuing of update checks. In this scenario, two update checks are
// done for one CRX. The second update check call is queued up and will run
// after the first check has completed. The CRX has no updates.
TEST_F(UpdateClientTest, OneCrxNoUpdateQueuedCall) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      static int num_call = 0;
      ++num_call;

      EXPECT_EQ(Error::NONE, error);

      if (num_call == 2)
        std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      auto& component = components.at(id);

      EXPECT_FALSE(component->is_foreground());

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "noupdate";
      ProtocolParser::Results results;
      results.list.push_back(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
  }

  std::vector<CrxUpdateItem> items1;
  auto receiver1 = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver1, Receive(_))
      .WillRepeatedly(
          [&items1](const CrxUpdateItem& item) { items1.push_back(item); });

  std::vector<CrxUpdateItem> items2;
  auto receiver2 = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver2, Receive(_))
      .WillRepeatedly(
          [&items2](const CrxUpdateItem& item) { items2.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver1),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver2),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(2u, items1.size());
  EXPECT_EQ(ComponentState::kChecking, items1[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items1[0].id.c_str());
  EXPECT_EQ(ComponentState::kUpToDate, items1[1].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items1[1].id.c_str());

  EXPECT_EQ(2u, items2.size());
  EXPECT_EQ(ComponentState::kChecking, items2[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items2[0].id.c_str());
  EXPECT_EQ(ComponentState::kUpToDate, items2[1].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items2[1].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the install of one CRX. Tests the installer is invoked with the
// run and arguments values of the manifest object. Tests that "pv" and "fp"
// are persisted.
TEST_F(UpdateClientTest, OneCrxInstall) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.0");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'
              run='UpdaterSetup.exe' arguments='--arg1 --arg2'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='7ab32f071cd9b5ef8e0d7913be161f532d98b3e9f
                                      a284a7cd8059c3409ce0498'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());

      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check[0]);
      EXPECT_EQ(1u, components.count(id));

      ProtocolParser::Result::Manifest::Package package;
      package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
      package.hash_sha256 =
          "7ab32f071cd9b5ef8e0d7913be161f532d98b3e9fa284a7cd8059c3409ce0498";
      package.fingerprint = "some-fingerprint";

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "ok";
      result.crx_urls.push_back(GURL("http://localhost/download/"));
      result.manifest.version = "1.0";
      result.manifest.browser_min_version = "11.0.1.0";
      result.manifest.run = "UpdaterSetup.exe";
      result.manifest.arguments = "--arg1 --arg2";
      result.manifest.packages.push_back(package);

      ProtocolParser::Results results;
      results.list.push_back(result);

      // Verify that calling Install sets ondemand.
      EXPECT_TRUE(components.at(id)->is_foreground());

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/jebgalgnebhfojomionfpkfelancnnkf.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 1843;
        download_metrics.total_bytes = 1843;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

        result.error = 0;
        result.response = path;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.0"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(0, ping_data[0].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);
  {
    EXPECT_FALSE(config()->GetPrefService()->FindPreference(
        "updateclientdata.apps.jebgalgnebhfojomionfpkfelancnnkf.pv"));
    EXPECT_FALSE(config()->GetPrefService()->FindPreference(
        "updateclientdata.apps.jebgalgnebhfojomionfpkfelancnnkf.fp"));
  }

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1)
        .WillOnce(Invoke([update_client](Events event, const std::string& id) {
          CrxUpdateItem update_item;
          ASSERT_TRUE(update_client->GetCrxUpdateState(id, &update_item));
          ASSERT_TRUE(update_item.component);
          const auto* test_installer = static_cast<TestInstaller*>(
              update_item.component->installer.get());
          EXPECT_STREQ("UpdaterSetup.exe",
                       test_installer->install_params()->run.c_str());
          EXPECT_STREQ("--arg1 --arg2",
                       test_installer->install_params()->arguments.c_str());
        }));
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  update_client->Install(
      std::string("jebgalgnebhfojomionfpkfelancnnkf"),
      base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(6u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[1].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[1].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[2].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[2].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[3].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[3].id.c_str());
  EXPECT_EQ(ComponentState::kUpdating, items[4].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[4].id.c_str());
  EXPECT_EQ(ComponentState::kUpdated, items[5].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[5].id.c_str());

  const base::DictionaryValue* dict =
      config()->GetPrefService()->GetDictionary("updateclientdata");
  std::string pv;
  dict->GetString("apps.jebgalgnebhfojomionfpkfelancnnkf.pv", &pv);
  EXPECT_STREQ("1.0", pv.c_str());
  std::string fingerprint;
  dict->GetString("apps.jebgalgnebhfojomionfpkfelancnnkf.fp", &fingerprint);
  EXPECT_STREQ("some-fingerprint", fingerprint.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the install of one CRX when no component data is provided. This
// results in an install error.
TEST_F(UpdateClientTest, OneCrxInstallNoCrxComponentData) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      return {base::nullopt};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      NOTREACHED();
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { NOTREACHED(); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      EXPECT_EQ(0u, MockPingManagerImpl::ping_data().size());
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          // Tests that the state of the component when the CrxComponent data
          // is not provided. In this case, the optional |item.component|
          // instance is not present.
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", item.id.c_str());
          EXPECT_FALSE(item.component);
          EXPECT_EQ(ErrorCategory::kService, item.error_category);
          EXPECT_EQ(static_cast<int>(Error::CRX_NOT_FOUND), item.error_code);
          EXPECT_EQ(0, item.extra_code1);
        }));
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  update_client->Install(
      std::string("jebgalgnebhfojomionfpkfelancnnkf"),
      base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(1u, items.size());
  EXPECT_EQ(ComponentState::kUpdateError, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests that overlapping installs of the same CRX result in an error.
TEST_F(UpdateClientTest, ConcurrentInstallSameCRX) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.0");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      static int num_call = 0;
      ++num_call;

      EXPECT_LE(num_call, 2);

      if (num_call == 1) {
        EXPECT_EQ(Error::UPDATE_IN_PROGRESS, error);
        return;
      }
      if (num_call == 2) {
        EXPECT_EQ(Error::NONE, error);
        std::move(quit_closure).Run();
      }
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "noupdate";

      ProtocolParser::Results results;
      results.list.push_back(result);

      // Verify that calling Install sets |is_foreground| for the component.
      EXPECT_TRUE(components.at(id)->is_foreground());

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);

  std::vector<CrxUpdateItem> items1;
  auto receiver1 = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver1, Receive(_))
      .WillRepeatedly(
          [&items1](const CrxUpdateItem& item) { items1.push_back(item); });

  std::vector<CrxUpdateItem> items2;
  auto receiver2 = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver2, Receive(_))
      .WillRepeatedly(
          [&items2](const CrxUpdateItem& item) { items2.push_back(item); });

  update_client->AddObserver(&observer);
  update_client->Install(
      std::string("jebgalgnebhfojomionfpkfelancnnkf"),
      base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver1),
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  update_client->Install(
      std::string("jebgalgnebhfojomionfpkfelancnnkf"),
      base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver2),
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(2u, items1.size());
  EXPECT_EQ(ComponentState::kChecking, items1[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items1[0].id.c_str());
  EXPECT_EQ(ComponentState::kUpToDate, items1[1].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items1[1].id.c_str());

  EXPECT_TRUE(items2.empty());

  update_client->RemoveObserver(&observer);
}

// Tests that UpdateClient::Update returns Error::INVALID_ARGUMENT when
// the |ids| parameter is empty.
TEST_F(UpdateClientTest, EmptyIdList) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      return {};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::INVALID_ARGUMENT, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      NOTREACHED();
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  const std::vector<std::string> empty_id_list;
  update_client->Update(
      empty_id_list, base::BindOnce(&DataCallbackMock::Callback), {}, false,
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();
}

TEST_F(UpdateClientTest, SendUninstallPing) {
  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return nullptr;
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      NOTREACHED();
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return nullptr;
    }

   private:
    MockCrxDownloader() : CrxDownloader(nullptr) {}
    ~MockCrxDownloader() override = default;

    void DoStartDownload(const GURL& url) override {}
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("1.2.3.4"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("0"), ping_data[0].next_version);
      EXPECT_EQ(10, ping_data[0].extra_code1);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  update_client->SendUninstallPing(
      "jebgalgnebhfojomionfpkfelancnnkf", base::Version("1.2.3.4"), 10,
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));

  RunThreads();
}

TEST_F(UpdateClientTest, RetryAfter) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      static int num_call = 0;
      ++num_call;

      EXPECT_LE(num_call, 4);

      if (num_call == 1) {
        EXPECT_EQ(Error::NONE, error);
      } else if (num_call == 2) {
        // This request is throttled since the update engine received a
        // positive |retry_after_sec| value in the update check response.
        EXPECT_EQ(Error::RETRY_LATER, error);
      } else if (num_call == 3) {
        // This request is a foreground Install, which is never throttled.
        // The update engine received a |retry_after_sec| value of 0, which
        // resets the throttling.
        EXPECT_EQ(Error::NONE, error);
      } else if (num_call == 4) {
        // This request succeeds since there is no throttling in effect.
        EXPECT_EQ(Error::NONE, error);
      }

      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());

      static int num_call = 0;
      ++num_call;

      EXPECT_LE(num_call, 3);

      int retry_after_sec(0);
      if (num_call == 1) {
        // Throttle the next call.
        retry_after_sec = 60 * 60;  // 1 hour.
      }

      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "noupdate";

      ProtocolParser::Results results;
      results.list.push_back(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, retry_after_sec));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;

  InSequence seq;
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  {
    // The engine handles this Update call but responds with a valid
    // |retry_after_sec|, which causes subsequent calls to fail.
    base::RunLoop runloop;
    update_client->Update(ids, base::BindOnce(&DataCallbackMock::Callback), {},
                          false,
                          base::BindOnce(&CompletionCallbackMock::Callback,
                                         runloop.QuitClosure()));
    runloop.Run();
  }

  {
    // This call will result in a completion callback invoked with
    // Error::ERROR_UPDATE_RETRY_LATER.
    base::RunLoop runloop;
    update_client->Update(ids, base::BindOnce(&DataCallbackMock::Callback), {},
                          false,
                          base::BindOnce(&CompletionCallbackMock::Callback,
                                         runloop.QuitClosure()));
    runloop.Run();
  }

  {
    // The Install call is handled, and the throttling is reset due to
    // the value of |retry_after_sec| in the completion callback.
    base::RunLoop runloop;
    update_client->Install(std::string("jebgalgnebhfojomionfpkfelancnnkf"),
                           base::BindOnce(&DataCallbackMock::Callback), {},
                           base::BindOnce(&CompletionCallbackMock::Callback,
                                          runloop.QuitClosure()));
    runloop.Run();
  }

  {
    // This call succeeds.
    base::RunLoop runloop;
    update_client->Update(ids, base::BindOnce(&DataCallbackMock::Callback), {},
                          false,
                          base::BindOnce(&CompletionCallbackMock::Callback,
                                         runloop.QuitClosure()));
    runloop.Run();
  }

  update_client->RemoveObserver(&observer);
}

// Tests the update check for two CRXs scenario. The first component supports
// the group policy to enable updates, and has its updates disabled. The second
// component has an update. The server does not honor the "updatedisabled"
// attribute and returns updates for both components. However, the update for
// the first component is not applied and the client responds with a
// (SERVICE_ERROR, UPDATE_DISABLED)
TEST_F(UpdateClientTest, TwoCrxUpdateOneUpdateDisabled) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeRefCounted<TestInstaller>();
      crx1.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      crx1.supports_group_policy_enable_component_updates = true;

      CrxComponent crx2;
      crx2.name = "test_ihfo";
      crx2.pk_hash.assign(ihfo_hash, ihfo_hash + base::size(ihfo_hash));
      crx2.version = base::Version("0.8");
      crx2.installer = base::MakeRefCounted<TestInstaller>();
      crx2.crx_format_requirement = crx_file::VerifierFormat::CRX3;

      return {crx1, crx2};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='7ab32f071cd9b5ef8e0d7913be161f532d98b3e9f
                                      a284a7cd8059c3409ce0498'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
        <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                         hash_sha256='8f5aa190311237cae00675af87ff457f278cd1a05
                                      895470ac5d46647d4a3c2ea'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */

      // UpdateClient reads the state of |enabled_component_updates| from the
      // configurator instance, persists its value in the corresponding
      // update context, and propagates it down to each of the update actions,
      // and further down to the UpdateChecker instance.
      EXPECT_FALSE(session_id.empty());
      EXPECT_FALSE(enabled_component_updates);
      EXPECT_EQ(2u, ids_to_check.size());

      ProtocolParser::Results results;
      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "7ab32f071cd9b5ef8e0d7913be161f532d98b3e9fa284a7cd8059c3409ce0498";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      }

      {
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[1]);
        EXPECT_EQ(1u, components.count(id));

        ProtocolParser::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "8f5aa190311237cae00675af87ff457f278cd1a05895470ac5d46647d4a3c2ea";

        ProtocolParser::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);
        results.list.push_back(result);
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadProgress,
                                    base::Unretained(this),
                                    download_metrics.downloaded_bytes,
                                    download_metrics.total_bytes));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      const auto ping_data = MockPingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(4, static_cast<int>(ping_data[0].error_category));
      EXPECT_EQ(2, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].next_version);
      EXPECT_EQ(0, static_cast<int>(ping_data[1].error_category));
      EXPECT_EQ(0, ping_data[1].error_code);
    }
  };

  // Disables updates for the components declaring support for the group policy.
  config()->SetEnabledComponentUpdates(false);
  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "ihfokbkgjpifnbbojhneepfflplebdkc"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(9u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kChecking, items[1].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[1].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[2].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[2].id.c_str());
  EXPECT_EQ(ComponentState::kUpdateError, items[3].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[3].id.c_str());
  EXPECT_EQ(ComponentState::kCanUpdate, items[4].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[4].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[5].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[5].id.c_str());
  EXPECT_EQ(ComponentState::kDownloading, items[6].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[6].id.c_str());
  EXPECT_EQ(ComponentState::kUpdating, items[7].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[7].id.c_str());
  EXPECT_EQ(ComponentState::kUpdated, items[8].state);
  EXPECT_STREQ("ihfokbkgjpifnbbojhneepfflplebdkc", items[8].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the scenario where the update check fails.
TEST_F(UpdateClientTest, OneCrxUpdateCheckFails) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      return {crx};
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::UPDATE_CHECK_ERROR, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(update_check_callback), base::nullopt,
                         ErrorCategory::kUpdateCheck, -1, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_EQ(5, static_cast<int>(item.error_category));
          EXPECT_EQ(-1, item.error_code);
          EXPECT_EQ(0, item.extra_code1);
        }));
  }

  std::vector<CrxUpdateItem> items;
  auto receiver = base::MakeRefCounted<MockCrxStateChangeReceiver>();
  EXPECT_CALL(*receiver, Receive(_))
      .WillRepeatedly(
          [&items](const CrxUpdateItem& item) { items.push_back(item); });

  update_client->AddObserver(&observer);
  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback),
      base::BindRepeating(&MockCrxStateChangeReceiver::Receive, receiver),
      false, base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));
  RunThreads();

  EXPECT_EQ(2u, items.size());
  EXPECT_EQ(ComponentState::kChecking, items[0].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[0].id.c_str());
  EXPECT_EQ(ComponentState::kUpdateError, items[1].state);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", items[1].id.c_str());

  update_client->RemoveObserver(&observer);
}

// Tests the scenario where the server responds with different values for
// application status.
TEST_F(UpdateClientTest, OneCrxErrorUnknownApp) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      std::vector<base::Optional<CrxComponent>> component;
      {
        CrxComponent crx;
        crx.name = "test_jebg";
        crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
        crx.version = base::Version("0.9");
        crx.installer = base::MakeRefCounted<TestInstaller>();
        crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
        component.push_back(crx);
      }
      {
        CrxComponent crx;
        crx.name = "test_abag";
        crx.pk_hash.assign(abag_hash, abag_hash + base::size(abag_hash));
        crx.version = base::Version("0.1");
        crx.installer = base::MakeRefCounted<TestInstaller>();
        crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
        component.push_back(crx);
      }
      {
        CrxComponent crx;
        crx.name = "test_ihfo";
        crx.pk_hash.assign(ihfo_hash, ihfo_hash + base::size(ihfo_hash));
        crx.version = base::Version("0.2");
        crx.installer = base::MakeRefCounted<TestInstaller>();
        crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
        component.push_back(crx);
      }
      {
        CrxComponent crx;
        crx.name = "test_gjpm";
        crx.pk_hash.assign(gjpm_hash, gjpm_hash + base::size(gjpm_hash));
        crx.version = base::Version("0.3");
        crx.installer = base::MakeRefCounted<TestInstaller>();
        crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
        component.push_back(crx);
      }
      return component;
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(4u, ids_to_check.size());

      const std::string update_response =
          ")]}'"
          R"({"response": {)"
          R"( "protocol": "3.1",)"
          R"( "app": [)"
          R"({"appid": "jebgalgnebhfojomionfpkfelancnnkf",)"
          R"( "status": "error-unknownApplication"},)"
          R"({"appid": "abagagagagagagagagagagagagagagag",)"
          R"( "status": "restricted"},)"
          R"({"appid": "ihfokbkgjpifnbbojhneepfflplebdkc",)"
          R"( "status": "error-invalidAppId"},)"
          R"({"appid": "gjpmebpgbhcamgdgjcmnjfhggjpgcimm",)"
          R"( "status": "error-foobarApp"})"
          R"(]}})";

      const auto parser = ProtocolHandlerFactoryJSON().CreateParser();
      EXPECT_TRUE(parser->Parse(update_response));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(update_check_callback), parser->results(),
                         ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_EQ(5, static_cast<int>(item.error_category));
          EXPECT_EQ(-10006, item.error_code);  // UNKNOWN_APPPLICATION.
          EXPECT_EQ(0, item.extra_code1);
        }));
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "abagagagagagagagagagagagagagagag"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "abagagagagagagagagagagagagagagag"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_EQ(5, static_cast<int>(item.error_category));
          EXPECT_EQ(-10007, item.error_code);  // RESTRICTED_APPLICATION.
          EXPECT_EQ(0, item.extra_code1);
        }));
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_EQ(5, static_cast<int>(item.error_category));
          EXPECT_EQ(-10008, item.error_code);  // INVALID_APPID.
          EXPECT_EQ(0, item.extra_code1);
        }));
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "gjpmebpgbhcamgdgjcmnjfhggjpgcimm"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_ERROR,
                                  "gjpmebpgbhcamgdgjcmnjfhggjpgcimm"))
        .Times(1)
        .WillOnce(Invoke([&update_client](Events event, const std::string& id) {
          CrxUpdateItem item;
          EXPECT_TRUE(update_client->GetCrxUpdateState(id, &item));
          EXPECT_EQ(ComponentState::kUpdateError, item.state);
          EXPECT_EQ(5, static_cast<int>(item.error_category));
          EXPECT_EQ(-10004, item.error_code);  // UPDATE_RESPONSE_NOT_FOUND.
          EXPECT_EQ(0, item.extra_code1);
        }));
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {
      "jebgalgnebhfojomionfpkfelancnnkf", "abagagagagagagagagagagagagagagag",
      "ihfokbkgjpifnbbojhneepfflplebdkc", "gjpmebpgbhcamgdgjcmnjfhggjpgcimm"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback), {}, true,
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests that a run action in invoked in the CRX install scenario.
TEST_F(UpdateClientTest, ActionRun_Install) {
  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='gjpmebpgbhcamgdgjcmnjfhggjpgcimm'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='runaction_test_win.crx3'
                         hash_sha256='89290a0d2ff21ca5b45e109c6cc859ab5fe294e19c102d54acd321429c372cea'/>
              </packages>
            </manifest>
            <actions>"
             <action run='ChromeRecovery.crx3'/>"
            </actions>"
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());

      const std::string id = "gjpmebpgbhcamgdgjcmnjfhggjpgcimm";
      EXPECT_EQ(id, ids_to_check[0]);
      EXPECT_EQ(1u, components.count(id));

      ProtocolParser::Result::Manifest::Package package;
      package.name = "runaction_test_win.crx3";
      package.hash_sha256 =
          "89290a0d2ff21ca5b45e109c6cc859ab5fe294e19c102d54acd321429c372cea";

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "ok";
      result.crx_urls.push_back(GURL("http://localhost/download/"));
      result.manifest.version = "1.0";
      result.manifest.browser_min_version = "11.0.1.0";
      result.manifest.packages.push_back(package);
      result.action_run = "ChromeRecovery.crx3";

      ProtocolParser::Results results;
      results.list.push_back(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/runaction_test_win.crx3") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 1843;
        download_metrics.total_bytes = 1843;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(
            MakeTestFile(TestFilePath("runaction_test_win.crx3"), &path));

        result.error = 0;
        result.response = path;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(&MockCrxDownloader::OnDownloadComplete,
                                    base::Unretained(this), true, result,
                                    download_metrics));
    }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      EXPECT_EQ(3u, events().size());

      /*
      "<event eventtype="14" eventresult="1" downloader="unknown" "
      "url="http://localhost/download/runaction_test_win.crx3"
      "downloaded=1843 "
      "total=1843 download_time_ms="1000" previousversion="0.0" "
      "nextversion="1.0"/>"
      */
      const auto& event0 = events()[0];
      EXPECT_EQ(14, event0.FindKey("eventtype")->GetInt());
      EXPECT_EQ(1, event0.FindKey("eventresult")->GetInt());
      EXPECT_EQ("unknown", event0.FindKey("downloader")->GetString());
      EXPECT_EQ("http://localhost/download/runaction_test_win.crx3",
                event0.FindKey("url")->GetString());
      EXPECT_EQ(1843, event0.FindKey("downloaded")->GetDouble());
      EXPECT_EQ(1843, event0.FindKey("total")->GetDouble());
      EXPECT_EQ(1000, event0.FindKey("download_time_ms")->GetDouble());
      EXPECT_EQ("0.0", event0.FindKey("previousversion")->GetString());
      EXPECT_EQ("1.0", event0.FindKey("nextversion")->GetString());

      // "<event eventtype="42" eventresult="1" errorcode="1877345072"/>"
      const auto& event1 = events()[1];
      EXPECT_EQ(42, event1.FindKey("eventtype")->GetInt());
      EXPECT_EQ(1, event1.FindKey("eventresult")->GetInt());
      EXPECT_EQ(1877345072, event1.FindKey("errorcode")->GetInt());

      // "<event eventtype=\"3\" eventresult=\"1\" previousversion=\"0.0\" "
      // "nextversion=\"1.0\"/>",
      const auto& event2 = events()[2];
      EXPECT_EQ(3, event2.FindKey("eventtype")->GetInt());
      EXPECT_EQ(1, event1.FindKey("eventresult")->GetInt());
      EXPECT_EQ("0.0", event0.FindKey("previousversion")->GetString());
      EXPECT_EQ("1.0", event0.FindKey("nextversion")->GetString());
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  update_client->Install(
      std::string("gjpmebpgbhcamgdgjcmnjfhggjpgcimm"),
      base::BindOnce([](const std::vector<std::string>& ids) {
        auto action_handler = base::MakeRefCounted<MockActionHandler>();
        EXPECT_CALL(*action_handler, Handle(_, _, _))
            .WillOnce([](const base::FilePath& action,
                         const std::string& session_id,
                         ActionHandler::Callback callback) {
              EXPECT_EQ("ChromeRecovery.crx3",
                        action.BaseName().MaybeAsASCII());
              EXPECT_TRUE(!session_id.empty());
              std::move(callback).Run(true, 1877345072, 0);
            });

        CrxComponent crx;
        crx.name = "test_niea";
        crx.pk_hash.assign(gjpm_hash, gjpm_hash + base::size(gjpm_hash));
        crx.version = base::Version("0.0");
        crx.installer = base::MakeRefCounted<VersionedTestInstaller>();
        crx.action_handler = action_handler;
        crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
        return std::vector<base::Optional<CrxComponent>>{crx};
      }),
      {},
      base::BindOnce(
          [](base::OnceClosure quit_closure, Error error) {
            EXPECT_EQ(Error::NONE, error);
            std::move(quit_closure).Run();
          },
          quit_closure()));

  RunThreads();
}

// Tests that a run action is invoked in an update scenario when there was
// no update.
TEST_F(UpdateClientTest, ActionRun_NoUpdate) {
  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      /*
      Mock the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.1'>
        <app appid='gjpmebpgbhcamgdgjcmnjfhggjpgcimm'>
          <updatecheck status='noupdate'>
            <actions>"
             <action run=ChromeRecovery.crx3'/>"
            </actions>"
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_FALSE(session_id.empty());
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "gjpmebpgbhcamgdgjcmnjfhggjpgcimm";
      EXPECT_EQ(id, ids_to_check[0]);
      EXPECT_EQ(1u, components.count(id));

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "noupdate";
      result.action_run = "ChromeRecovery.crx3";

      ProtocolParser::Results results;
      results.list.push_back(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override {
      EXPECT_EQ(1u, events().size());

      // "<event eventtype="42" eventresult="1" errorcode="1877345072"/>"
      const auto& event = events()[0];
      EXPECT_EQ(42, event.FindKey("eventtype")->GetInt());
      EXPECT_EQ(1, event.FindKey("eventresult")->GetInt());
      EXPECT_EQ(1877345072, event.FindKey("errorcode")->GetInt());
    }
  };

  // Unpack the CRX to mock an existing install to be updated. The action to
  // run is going to be resolved relative to this directory.
  base::FilePath unpack_path;
  {
    base::RunLoop runloop;
    base::OnceClosure quit_closure = runloop.QuitClosure();

    auto config = base::MakeRefCounted<TestConfigurator>();
    auto component_unpacker = base::MakeRefCounted<ComponentUnpacker>(
        std::vector<uint8_t>(std::begin(gjpm_hash), std::end(gjpm_hash)),
        TestFilePath("runaction_test_win.crx3"), nullptr,
        config->GetUnzipperFactory()->Create(),
        config->GetPatcherFactory()->Create(), crx_file::VerifierFormat::CRX3);

    component_unpacker->Unpack(base::BindOnce(
        [](base::FilePath* unpack_path, base::OnceClosure quit_closure,
           const ComponentUnpacker::Result& result) {
          EXPECT_EQ(UnpackerError::kNone, result.error);
          EXPECT_EQ(0, result.extended_error);
          *unpack_path = result.unpack_path;
          std::move(quit_closure).Run();
        },
        &unpack_path, runloop.QuitClosure()));

    runloop.Run();
  }

  EXPECT_FALSE(unpack_path.empty());
  EXPECT_TRUE(base::DirectoryExists(unpack_path));
  int64_t file_size = 0;
  EXPECT_TRUE(base::GetFileSize(unpack_path.AppendASCII("ChromeRecovery.crx3"),
                                &file_size));
  EXPECT_EQ(44582, file_size);

  base::ScopedTempDir unpack_path_owner;
  EXPECT_TRUE(unpack_path_owner.Set(unpack_path));

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  const std::vector<std::string> ids = {"gjpmebpgbhcamgdgjcmnjfhggjpgcimm"};
  update_client->Update(
      ids,
      base::BindOnce(
          [](const base::FilePath& unpack_path,
             const std::vector<std::string>& ids) {
            auto action_handler = base::MakeRefCounted<MockActionHandler>();
            EXPECT_CALL(*action_handler, Handle(_, _, _))
                .WillOnce([](const base::FilePath& action,
                             const std::string& session_id,
                             ActionHandler::Callback callback) {
                  EXPECT_EQ("ChromeRecovery.crx3",
                            action.BaseName().MaybeAsASCII());
                  EXPECT_TRUE(!session_id.empty());
                  std::move(callback).Run(true, 1877345072, 0);
                });

            CrxComponent crx;
            crx.name = "test_niea";
            crx.pk_hash.assign(gjpm_hash, gjpm_hash + base::size(gjpm_hash));
            crx.version = base::Version("1.0");
            crx.installer =
                base::MakeRefCounted<ReadOnlyTestInstaller>(unpack_path);
            crx.action_handler = action_handler;
            crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
            return std::vector<base::Optional<CrxComponent>>{crx};
          },
          unpack_path),
      {}, false,
      base::BindOnce(
          [](base::OnceClosure quit_closure, Error error) {
            EXPECT_EQ(Error::NONE, error);
            std::move(quit_closure).Run();
          },
          quit_closure()));

  RunThreads();
}

// Tests that custom response attributes are visible to observers.
TEST_F(UpdateClientTest, CustomAttributeNoUpdate) {
  class DataCallbackMock {
   public:
    static std::vector<base::Optional<CrxComponent>> Callback(
        const std::vector<std::string>& ids) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + base::size(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeRefCounted<TestInstaller>();
      crx.crx_format_requirement = crx_file::VerifierFormat::CRX3;
      std::vector<base::Optional<CrxComponent>> component = {crx};
      return component;
    }
  };

  class CompletionCallbackMock {
   public:
    static void Callback(base::OnceClosure quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      std::move(quit_closure).Run();
    }
  };

  class MockUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        scoped_refptr<Configurator> config,
        PersistedData* metadata) {
      return std::make_unique<MockUpdateChecker>();
    }

    void CheckForUpdates(
        const std::string& session_id,
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const base::flat_map<std::string, std::string>& additional_attributes,
        bool enabled_component_updates,
        UpdateCheckCallback update_check_callback) override {
      EXPECT_FALSE(session_id.empty());
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      auto& component = components.at(id);

      EXPECT_TRUE(component->is_foreground());

      ProtocolParser::Result result;
      result.extension_id = id;
      result.status = "noupdate";
      result.custom_attributes["_example"] = "example_value";

      ProtocolParser::Results results;
      results.list.push_back(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::BindOnce(std::move(update_check_callback), results,
                                    ErrorCategory::kNone, 0, 0));
    }
  };

  class MockCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        scoped_refptr<NetworkFetcherFactory> network_fetcher_factory) {
      return std::make_unique<MockCrxDownloader>();
    }

    MockCrxDownloader() : CrxDownloader(nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class MockPingManager : public MockPingManagerImpl {
   public:
    explicit MockPingManager(scoped_refptr<Configurator> config)
        : MockPingManagerImpl(config) {}

   protected:
    ~MockPingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeRefCounted<UpdateClientImpl>(
          config(), base::MakeRefCounted<MockPingManager>(config()),
          &MockUpdateChecker::Create, &MockCrxDownloader::Create);

  class Observer : public UpdateClient::Observer {
   public:
    explicit Observer(scoped_refptr<UpdateClient> update_client)
        : update_client_(update_client) {}

    void OnEvent(Events event, const std::string& id) override {
      if (event != Events::COMPONENT_NOT_UPDATED)
        return;
      ++calls;
      CrxUpdateItem item;
      EXPECT_TRUE(update_client_->GetCrxUpdateState(
          "jebgalgnebhfojomionfpkfelancnnkf", &item));
      EXPECT_EQ("example_value", item.custom_updatecheck_data["_example"]);
    }

    int calls = 0;

   private:
    scoped_refptr<UpdateClient> update_client_;
  };

  Observer observer(update_client);
  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::BindOnce(&DataCallbackMock::Callback), {}, true,
      base::BindOnce(&CompletionCallbackMock::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);

  EXPECT_EQ(1, observer.calls);
}
}  // namespace update_client
