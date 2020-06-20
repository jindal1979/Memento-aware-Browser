// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/network_diagnostics/dns_resolution_routine.h"

#include <deque>

#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/session_manager/core/session_manager.h"
#include "content/public/test/browser_task_environment.h"
#include "services/network/test/test_network_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/shill/dbus-constants.h"

namespace chromeos {
namespace network_diagnostics {

namespace {

const int kFakePortNumber = 1234;
const char kFakeTestProfile[] = "test";

net::IPEndPoint FakeIPAddress() {
  return net::IPEndPoint(net::IPAddress::IPv4Localhost(), kFakePortNumber);
}

class FakeHostResolver : public network::mojom::HostResolver {
 public:
  struct DnsResult {
    DnsResult(int32_t result,
              net::ResolveErrorInfo resolve_error_info,
              base::Optional<net::AddressList> resolved_addresses)
        : result(result),
          resolve_error_info(resolve_error_info),
          resolved_addresses(resolved_addresses) {}

    int result;
    net::ResolveErrorInfo resolve_error_info;
    base::Optional<net::AddressList> resolved_addresses;
  };

  FakeHostResolver(mojo::PendingReceiver<network::mojom::HostResolver> receiver,
                   std::deque<DnsResult*> fake_dns_results)
      : receiver_(this, std::move(receiver)),
        fake_dns_results_(std::move(fake_dns_results)) {}
  ~FakeHostResolver() override {}

  // network::mojom::HostResolver
  void ResolveHost(const net::HostPortPair& host,
                   const net::NetworkIsolationKey& network_isolation_key,
                   network::mojom::ResolveHostParametersPtr optional_parameters,
                   mojo::PendingRemote<network::mojom::ResolveHostClient>
                       pending_response_client) override {
    mojo::Remote<network::mojom::ResolveHostClient> response_client(
        std::move(pending_response_client));
    DnsResult* result = fake_dns_results_.front();
    DCHECK(result);
    fake_dns_results_.pop_front();
    response_client->OnComplete(result->result, result->resolve_error_info,
                                result->resolved_addresses);
  }
  void MdnsListen(
      const net::HostPortPair& host,
      net::DnsQueryType query_type,
      mojo::PendingRemote<network::mojom::MdnsListenClient> response_client,
      MdnsListenCallback callback) override {
    NOTREACHED();
  }

 private:
  mojo::Receiver<network::mojom::HostResolver> receiver_;
  // Use the list of fake dns results to fake different responses for multiple
  // calls to the host_resolver's ResolveHost().
  std::deque<DnsResult*> fake_dns_results_;
};

class FakeNetworkContext : public network::TestNetworkContext {
 public:
  FakeNetworkContext() = default;

  explicit FakeNetworkContext(
      std::deque<FakeHostResolver::DnsResult*> fake_dns_results)
      : fake_dns_results_(std::move(fake_dns_results)) {}

  ~FakeNetworkContext() override {}

  // network::TestNetworkContext:
  void CreateHostResolver(
      const base::Optional<net::DnsConfigOverrides>& config_overrides,
      mojo::PendingReceiver<network::mojom::HostResolver> receiver) override {
    ASSERT_FALSE(resolver_);
    resolver_ = std::make_unique<FakeHostResolver>(
        std::move(receiver), std::move(fake_dns_results_));
  }

 private:
  std::unique_ptr<FakeHostResolver> resolver_;
  std::deque<FakeHostResolver::DnsResult*> fake_dns_results_;
};

}  // namespace

class DnsResolutionRoutineTest : public ::testing::Test {
 public:
  DnsResolutionRoutineTest()
      : profile_manager_(TestingBrowserProcess::GetGlobal()) {
    session_manager::SessionManager::Get()->SetSessionState(
        session_manager::SessionState::LOGIN_PRIMARY);
  }
  DnsResolutionRoutineTest(const DnsResolutionRoutineTest&) = delete;
  DnsResolutionRoutineTest& operator=(const DnsResolutionRoutineTest&) = delete;

  void RunRoutine(
      mojom::RoutineVerdict expected_routine_verdict,
      const std::vector<mojom::DnsResolutionProblem>& expected_problems) {
    dns_resolution_routine_->RunRoutine(base::BindOnce(
        &DnsResolutionRoutineTest::CompareVerdict, weak_factory_.GetWeakPtr(),
        expected_routine_verdict, expected_problems));
    run_loop_.Run();
  }

  void CompareVerdict(
      mojom::RoutineVerdict expected_verdict,
      const std::vector<mojom::DnsResolutionProblem>& expected_problems,
      mojom::RoutineVerdict actual_verdict,
      const std::vector<mojom::DnsResolutionProblem>& actual_problems) {
    DCHECK(run_loop_.running());
    EXPECT_EQ(expected_verdict, actual_verdict);
    EXPECT_EQ(expected_problems, actual_problems);
    run_loop_.Quit();
  }

  void SetUpFakeProperties(
      std::deque<FakeHostResolver::DnsResult*> fake_dns_results) {
    ASSERT_TRUE(profile_manager_.SetUp());

    fake_network_context_ =
        std::make_unique<FakeNetworkContext>(std::move(fake_dns_results));
    test_profile_ = profile_manager_.CreateTestingProfile(kFakeTestProfile);
  }

  void SetUpDnsResolutionRoutine() {
    dns_resolution_routine_ = std::make_unique<DnsResolutionRoutine>();
    dns_resolution_routine_->set_network_context_for_testing(
        fake_network_context_.get());
    dns_resolution_routine_->set_profile_for_testing(test_profile_);
  }

  // Sets up required properties (via fakes) and runs the test.
  //
  // Parameters:
  // |fake_dns_results|: Represents the results of a one or more DNS
  // resolutions. |expected_routine_verdict|: Represents the expected verdict
  // reported by this test. |expected_problems|: Represents the expected problem
  // reported by this test.
  void SetUpAndRunRoutine(
      std::deque<FakeHostResolver::DnsResult*> fake_dns_results,
      mojom::RoutineVerdict expected_routine_verdict,
      const std::vector<mojom::DnsResolutionProblem>& expected_problems) {
    SetUpFakeProperties(std::move(fake_dns_results));
    SetUpDnsResolutionRoutine();
    RunRoutine(expected_routine_verdict, expected_problems);
  }

 private:
  content::BrowserTaskEnvironment task_environment_;
  base::RunLoop run_loop_;
  session_manager::SessionManager session_manager_;
  std::unique_ptr<FakeNetworkContext> fake_network_context_;
  // Unowned
  Profile* test_profile_;
  TestingProfileManager profile_manager_;
  std::unique_ptr<DnsResolutionRoutine> dns_resolution_routine_;
  base::WeakPtrFactory<DnsResolutionRoutineTest> weak_factory_{this};
};

// A passing routine requires an error code of net::OK and a non-empty
// net::AddressList for the DNS resolution.
TEST_F(DnsResolutionRoutineTest, TestSuccessfulResolution) {
  std::deque<FakeHostResolver::DnsResult*> fake_dns_results;
  auto successful_resolution = std::make_unique<FakeHostResolver::DnsResult>(
      net::OK, net::ResolveErrorInfo(net::OK),
      net::AddressList(FakeIPAddress()));
  fake_dns_results.push_back(successful_resolution.get());
  SetUpAndRunRoutine(std::move(fake_dns_results),
                     mojom::RoutineVerdict::kNoProblem, {});
}

// Set up the |fake_dns_results| to return a DnsResult with an error code
// net::ERR_NAME_NOT_RESOLVED faking a failed DNS resolution.
TEST_F(DnsResolutionRoutineTest, TestResolutionFailure) {
  std::deque<FakeHostResolver::DnsResult*> fake_dns_results;
  auto failed_resolution = std::make_unique<FakeHostResolver::DnsResult>(
      net::ERR_NAME_NOT_RESOLVED,
      net::ResolveErrorInfo(net::ERR_NAME_NOT_RESOLVED), net::AddressList());
  fake_dns_results.push_back(failed_resolution.get());
  SetUpAndRunRoutine(std::move(fake_dns_results),
                     mojom::RoutineVerdict::kProblem,
                     {mojom::DnsResolutionProblem::kFailedToResolveHost});
}

// Set up the |fake_dns_results| to first return a DnsResult with an error code
// net::ERR_DNS_TIMED_OUT faking a timed out DNS resolution. On the second
// host resolution attempt, fake a net::OK resolution.
TEST_F(DnsResolutionRoutineTest, TestSuccessOnRetry) {
  std::deque<FakeHostResolver::DnsResult*> fake_dns_results;
  auto timed_out_resolution = std::make_unique<FakeHostResolver::DnsResult>(
      net::ERR_DNS_TIMED_OUT, net::ResolveErrorInfo(net::ERR_DNS_TIMED_OUT),
      net::AddressList());
  fake_dns_results.push_back(timed_out_resolution.get());
  auto successful_resolution = std::make_unique<FakeHostResolver::DnsResult>(
      net::OK, net::ResolveErrorInfo(net::OK),
      net::AddressList(FakeIPAddress()));
  fake_dns_results.push_back(successful_resolution.get());

  fake_dns_results.push_back(successful_resolution.get());
  SetUpAndRunRoutine(std::move(fake_dns_results),
                     mojom::RoutineVerdict::kNoProblem, {});
}

}  // namespace network_diagnostics
}  // namespace chromeos
