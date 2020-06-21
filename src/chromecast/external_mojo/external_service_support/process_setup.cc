// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/external_mojo/external_service_support/process_setup.h"

#include <locale.h>
#include <signal.h>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "chromecast/chromecast_buildflags.h"

#if !defined(OS_ANDROID) && !defined(OS_FUCHSIA)
#include "chromecast/external_mojo/external_service_support/crash_reporter_client.h"
#endif

namespace chromecast {
namespace external_service_support {

void CommonProcessInitialization(int argc, char** argv) {
#if !defined(OS_ANDROID)
  // Set C library locale to make sure CommandLine can parse argument values
  // in the correct encoding.
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");
#endif

  base::CommandLine::Init(argc, argv);

  logging::LoggingSettings settings;
  settings.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  logging::InitLogging(settings);

#if BUILDFLAG(IS_CAST_DESKTOP_BUILD)
  logging::SetLogItems(true, true, true, false);
#else
  // Timestamp available through logcat -v time.
  logging::SetLogItems(true, true, false, false);
#endif  // BUILDFLAG(IS_CAST_DESKTOP_BUILD)

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  base::FeatureList::InitializeInstance(
      command_line->GetSwitchValueASCII(switches::kEnableFeatures),
      command_line->GetSwitchValueASCII(switches::kDisableFeatures));
#if !defined(OFFICIAL_BUILD)
  base::debug::EnableInProcessStackDumping();
#endif  // !defined(OFFICIAL_BUILD)

#if !defined(OS_ANDROID) && !defined(OS_FUCHSIA)
  CrashReporterClient::InitCrashReporter();
#endif

  CHECK_NE(SIG_ERR, signal(SIGPIPE, SIG_IGN));
}

}  // namespace external_service_support
}  // namespace chromecast
