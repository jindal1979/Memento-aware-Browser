// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/cros_healthd/public/cpp/service_connection.h"

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/test/bind_test_util.h"
#include "base/test/task_environment.h"
#include "chromeos/dbus/cros_healthd/cros_healthd_client.h"
#include "chromeos/dbus/cros_healthd/fake_cros_healthd_client.h"
#include "chromeos/services/cros_healthd/public/mojom/cros_healthd.mojom.h"
#include "chromeos/services/cros_healthd/public/mojom/cros_healthd_diagnostics.mojom.h"
#include "chromeos/services/cros_healthd/public/mojom/cros_healthd_probe.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArgs;

namespace chromeos {
namespace cros_healthd {
namespace {

std::vector<mojom::DiagnosticRoutineEnum> MakeAvailableRoutines() {
  return std::vector<mojom::DiagnosticRoutineEnum>{
      mojom::DiagnosticRoutineEnum::kUrandom,
      mojom::DiagnosticRoutineEnum::kBatteryCapacity,
      mojom::DiagnosticRoutineEnum::kBatteryHealth,
      mojom::DiagnosticRoutineEnum::kSmartctlCheck,
      mojom::DiagnosticRoutineEnum::kCpuCache,
      mojom::DiagnosticRoutineEnum::kCpuStress,
      mojom::DiagnosticRoutineEnum::kFloatingPointAccuracy,
      mojom::DiagnosticRoutineEnum::kNvmeWearLevel,
      mojom::DiagnosticRoutineEnum::kNvmeSelfTest,
  };
}

mojom::RunRoutineResponsePtr MakeRunRoutineResponse() {
  return mojom::RunRoutineResponse::New(
      /*id=*/13, /*status=*/mojom::DiagnosticRoutineStatusEnum::kReady);
}

mojom::RoutineUpdatePtr MakeInteractiveRoutineUpdate() {
  mojom::InteractiveRoutineUpdate interactive_update(
      /*user_message=*/mojom::DiagnosticRoutineUserMessageEnum::kUnplugACPower);

  mojom::RoutineUpdateUnion update_union;
  update_union.set_interactive_update(interactive_update.Clone());

  return mojom::RoutineUpdate::New(
      /*progress_percent=*/42,
      /*output=*/mojo::ScopedHandle(), update_union.Clone());
}

mojom::RoutineUpdatePtr MakeNonInteractiveRoutineUpdate() {
  mojom::NonInteractiveRoutineUpdate noninteractive_update(
      /*status=*/mojom::DiagnosticRoutineStatusEnum::kRunning,
      /*status_message=*/"status_message");

  mojom::RoutineUpdateUnion update_union;
  update_union.set_noninteractive_update(noninteractive_update.Clone());

  return mojom::RoutineUpdate::New(
      /*progress_percent=*/43,
      /*output=*/mojo::ScopedHandle(), update_union.Clone());
}

class MockCrosHealthdBluetoothObserver
    : public mojom::CrosHealthdBluetoothObserver {
 public:
  MockCrosHealthdBluetoothObserver() : receiver_{this} {}
  MockCrosHealthdBluetoothObserver(const MockCrosHealthdBluetoothObserver&) =
      delete;
  MockCrosHealthdBluetoothObserver& operator=(
      const MockCrosHealthdBluetoothObserver&) = delete;

  MOCK_METHOD(void, OnAdapterAdded, (), (override));
  MOCK_METHOD(void, OnAdapterRemoved, (), (override));
  MOCK_METHOD(void, OnAdapterPropertyChanged, (), (override));
  MOCK_METHOD(void, OnDeviceAdded, (), (override));
  MOCK_METHOD(void, OnDeviceRemoved, (), (override));
  MOCK_METHOD(void, OnDevicePropertyChanged, (), (override));

  mojo::PendingRemote<mojom::CrosHealthdBluetoothObserver> pending_remote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

 private:
  mojo::Receiver<mojom::CrosHealthdBluetoothObserver> receiver_;
};

class MockCrosHealthdLidObserver : public mojom::CrosHealthdLidObserver {
 public:
  MockCrosHealthdLidObserver() : receiver_{this} {}
  MockCrosHealthdLidObserver(const MockCrosHealthdLidObserver&) = delete;
  MockCrosHealthdLidObserver& operator=(const MockCrosHealthdLidObserver&) =
      delete;

  MOCK_METHOD(void, OnLidClosed, (), (override));
  MOCK_METHOD(void, OnLidOpened, (), (override));

  mojo::PendingRemote<mojom::CrosHealthdLidObserver> pending_remote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

 private:
  mojo::Receiver<mojom::CrosHealthdLidObserver> receiver_;
};

class MockCrosHealthdPowerObserver : public mojom::CrosHealthdPowerObserver {
 public:
  MockCrosHealthdPowerObserver() : receiver_{this} {}
  MockCrosHealthdPowerObserver(const MockCrosHealthdPowerObserver&) = delete;
  MockCrosHealthdPowerObserver& operator=(const MockCrosHealthdPowerObserver&) =
      delete;

  MOCK_METHOD(void, OnAcInserted, (), (override));
  MOCK_METHOD(void, OnAcRemoved, (), (override));
  MOCK_METHOD(void, OnOsSuspend, (), (override));
  MOCK_METHOD(void, OnOsResume, (), (override));

  mojo::PendingRemote<mojom::CrosHealthdPowerObserver> pending_remote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

 private:
  mojo::Receiver<mojom::CrosHealthdPowerObserver> receiver_;
};

class CrosHealthdServiceConnectionTest : public testing::Test {
 public:
  CrosHealthdServiceConnectionTest() = default;

  void SetUp() override { CrosHealthdClient::InitializeFake(); }

  void TearDown() override {
    CrosHealthdClient::Shutdown();

    // Wait for ServiceConnection to observe the destruction of the client.
    base::RunLoop().RunUntilIdle();
  }

 private:
  base::test::TaskEnvironment task_environment_;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthdServiceConnectionTest);
};

TEST_F(CrosHealthdServiceConnectionTest, GetAvailableRoutines) {
  // Test that we can retrieve a list of available routines.
  auto routines = MakeAvailableRoutines();
  FakeCrosHealthdClient::Get()->SetAvailableRoutinesForTesting(routines);
  bool callback_done = false;
  ServiceConnection::GetInstance()->GetAvailableRoutines(base::BindOnce(
      [](bool* callback_done,
         const std::vector<mojom::DiagnosticRoutineEnum>& response) {
        EXPECT_EQ(response, MakeAvailableRoutines());
        *callback_done = true;
      },
      &callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

TEST_F(CrosHealthdServiceConnectionTest, GetRoutineUpdate) {
  // Test that we can get an interactive routine update.
  auto interactive_update = MakeInteractiveRoutineUpdate();
  FakeCrosHealthdClient::Get()->SetGetRoutineUpdateResponseForTesting(
      interactive_update);
  bool callback_done = false;
  ServiceConnection::GetInstance()->GetRoutineUpdate(
      /*id=*/542, /*command=*/mojom::DiagnosticRoutineCommandEnum::kGetStatus,
      /*include_output=*/true,
      base::BindOnce(
          [](bool* callback_done, mojom::RoutineUpdatePtr response) {
            EXPECT_EQ(response, MakeInteractiveRoutineUpdate());
            *callback_done = true;
          },
          &callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);

  // Test that we can get a noninteractive routine update.
  auto noninteractive_update = MakeNonInteractiveRoutineUpdate();
  FakeCrosHealthdClient::Get()->SetGetRoutineUpdateResponseForTesting(
      noninteractive_update);
  callback_done = false;
  ServiceConnection::GetInstance()->GetRoutineUpdate(
      /*id=*/543, /*command=*/mojom::DiagnosticRoutineCommandEnum::kCancel,
      /*include_output=*/false,
      base::BindOnce(
          [](bool* callback_done, mojom::RoutineUpdatePtr response) {
            EXPECT_EQ(response, MakeNonInteractiveRoutineUpdate());
            *callback_done = true;
          },
          &callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

TEST_F(CrosHealthdServiceConnectionTest, RunUrandomRoutine) {
  // Test that we can run the urandom routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  bool callback_done = false;
  ServiceConnection::GetInstance()->RunUrandomRoutine(
      /*length_seconds=*/10,
      base::BindOnce(
          [](bool* callback_done, mojom::RunRoutineResponsePtr response) {
            EXPECT_EQ(response, MakeRunRoutineResponse());
            *callback_done = true;
          },
          &callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

TEST_F(CrosHealthdServiceConnectionTest, RunBatteryCapacityRoutine) {
  // Test that we can run the battery capacity routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  bool callback_done = false;
  ServiceConnection::GetInstance()->RunBatteryCapacityRoutine(
      /*low_mah=*/1001, /*high_mah=*/120345,
      base::BindOnce(
          [](bool* callback_done, mojom::RunRoutineResponsePtr response) {
            EXPECT_EQ(response, MakeRunRoutineResponse());
            *callback_done = true;
          },
          &callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

TEST_F(CrosHealthdServiceConnectionTest, RunBatteryHealthRoutine) {
  // Test that we can run the battery health routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  bool callback_done = false;
  ServiceConnection::GetInstance()->RunBatteryHealthRoutine(
      /*maximum_cycle_count=*/2, /*percent_battery_wear_allowed=*/90,
      base::BindOnce(
          [](bool* callback_done, mojom::RunRoutineResponsePtr response) {
            EXPECT_EQ(response, MakeRunRoutineResponse());
            *callback_done = true;
          },
          &callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

TEST_F(CrosHealthdServiceConnectionTest, RunSmartctlCheckRoutine) {
  // Test that we can run the smartctl check routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  bool callback_done = false;
  ServiceConnection::GetInstance()->RunSmartctlCheckRoutine(base::BindOnce(
      [](bool* callback_done, mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        *callback_done = true;
      },
      &callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

TEST_F(CrosHealthdServiceConnectionTest, RunAcPowerRoutine) {
  // Test that we can run the AC power routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->RunAcPowerRoutine(
      mojom::AcPowerStatusEnum::kConnected,
      /*expected_power_type=*/"power_type",
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunCpuCacheRoutine) {
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->RunCpuCacheRoutine(
      base::TimeDelta().FromSeconds(10),
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunCpuStressRoutine) {
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->RunCpuStressRoutine(
      base::TimeDelta().FromSeconds(10),
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunFloatingPointAccuracyRoutine) {
  // Test that we can run the floating point accuracy routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->RunFloatingPointAccuracyRoutine(
      /*exec_duration=*/base::TimeDelta::FromSeconds(10),
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunNvmeWearLevelRoutine) {
  // Test that we can run the NVMe wear-level routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->RunNvmeWearLevelRoutine(
      /*wear_level_threshold=*/50,
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunNvmeSelfTestRoutine) {
  // Test that we can run the NVMe self-test routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->RunNvmeSelfTestRoutine(
      mojom::NvmeSelfTestTypeEnum::kShortSelfTest,
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunDiskReadRoutine) {
  // Test that we can run the disk read routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  base::TimeDelta exec_duration = base::TimeDelta().FromSeconds(10);
  ServiceConnection::GetInstance()->RunDiskReadRoutine(
      mojom::DiskReadRoutineTypeEnum::kLinearRead,
      /*exec_duration=*/exec_duration, /*file_size_mb=*/1024,
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunPrimeSearchRoutine) {
  // Test that we can run the prime search routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  base::TimeDelta exec_duration = base::TimeDelta().FromSeconds(10);
  ServiceConnection::GetInstance()->RunPrimeSearchRoutine(
      /*exec_duration=*/exec_duration, /*max_num=*/1000000,
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, RunBatteryDischargeRoutine) {
  // Test that we can run the battery discharge routine.
  auto response = MakeRunRoutineResponse();
  FakeCrosHealthdClient::Get()->SetRunRoutineResponseForTesting(response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->RunBatteryDischargeRoutine(
      /*exec_duration=*/base::TimeDelta::FromSeconds(12),
      /*maximum_discharge_percent_allowed=*/99,
      base::BindLambdaForTesting([&](mojom::RunRoutineResponsePtr response) {
        EXPECT_EQ(response, MakeRunRoutineResponse());
        run_loop.Quit();
      }));
  run_loop.Run();
}

// Test that we can add a Bluetooth observer.
TEST_F(CrosHealthdServiceConnectionTest, AddBluetoothObserver) {
  MockCrosHealthdBluetoothObserver observer;
  ServiceConnection::GetInstance()->AddBluetoothObserver(
      observer.pending_remote());

  // Send out an event to verify the observer is connected.
  base::RunLoop run_loop;
  EXPECT_CALL(observer, OnAdapterAdded()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));
  FakeCrosHealthdClient::Get()->EmitAdapterAddedEventForTesting();

  run_loop.Run();
}

// Test that we can add a lid observer.
TEST_F(CrosHealthdServiceConnectionTest, AddLidObserver) {
  MockCrosHealthdLidObserver observer;
  ServiceConnection::GetInstance()->AddLidObserver(observer.pending_remote());

  // Send out an event to make sure the observer is connected.
  base::RunLoop run_loop;
  EXPECT_CALL(observer, OnLidClosed()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));
  FakeCrosHealthdClient::Get()->EmitLidClosedEventForTesting();

  run_loop.Run();
}

// Test that we can add a power observer.
TEST_F(CrosHealthdServiceConnectionTest, AddPowerObserver) {
  MockCrosHealthdPowerObserver observer;
  ServiceConnection::GetInstance()->AddPowerObserver(observer.pending_remote());

  // Send out an event to make sure the observer is connected.
  base::RunLoop run_loop;
  EXPECT_CALL(observer, OnAcInserted()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));
  FakeCrosHealthdClient::Get()->EmitAcInsertedEventForTesting();

  run_loop.Run();
}

TEST_F(CrosHealthdServiceConnectionTest, ProbeTelemetryInfo) {
  auto response = mojom::TelemetryInfo::New();
  FakeCrosHealthdClient::Get()->SetProbeTelemetryInfoResponseForTesting(
      response);
  base::RunLoop run_loop;
  ServiceConnection::GetInstance()->ProbeTelemetryInfo(
      {}, base::BindLambdaForTesting([&](mojom::TelemetryInfoPtr info) {
        EXPECT_EQ(info, response);
        run_loop.Quit();
      }));
  run_loop.Run();
}

}  // namespace
}  // namespace cros_healthd
}  // namespace chromeos
