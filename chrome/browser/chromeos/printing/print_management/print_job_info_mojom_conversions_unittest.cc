// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/printing/print_management/print_job_info_mojom_conversions.h"

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/printing/cups_print_job.h"
#include "chrome/browser/chromeos/printing/history/print_job_info.pb.h"
#include "chrome/browser/chromeos/printing/printer_error_codes.h"
#include "chrome/browser/printing/print_job.h"
#include "chromeos/components/print_management/mojom/printing_manager.mojom.h"
#include "chromeos/printing/printer_configuration.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace proto = printing::proto;
namespace mojom = printing::printing_manager::mojom;

namespace {

constexpr char kName[] = "name";
constexpr char kUri[] = "ipp://192.168.1.5";
constexpr char kTitle[] = "title";
constexpr char kId[] = "id";
constexpr char kPrinterId[] = "printerId";
constexpr int64_t kJobCreationTime = 0;
constexpr uint32_t kPagesNumber = 3;
constexpr uint32_t kPrintedPageNumber = 1;

proto::PrintJobInfo CreatePrintJobInfoProto() {
  // Create Printer proto.
  proto::Printer printer;
  printer.set_name(kName);
  printer.set_uri(kUri);

  // Create PrintJobInfo proto.
  proto::PrintJobInfo print_job_info;

  print_job_info.set_id(kId);
  print_job_info.set_title(kTitle);
  print_job_info.set_status(
      printing::proto::PrintJobInfo_PrintJobStatus_PRINTED);
  print_job_info.set_printer_error_code(
      printing::proto::PrintJobInfo_PrinterErrorCode_NO_ERROR);
  print_job_info.set_creation_time(
      static_cast<int64_t>(base::Time::UnixEpoch().ToJsTime()));
  print_job_info.set_number_of_pages(kPagesNumber);
  *print_job_info.mutable_printer() = printer;

  return print_job_info;
}

std::unique_ptr<CupsPrintJob> CreateCupsPrintJob() {
  Printer printer;
  printer.set_display_name(kName);
  printer.set_uri(kUri);
  printer.set_id(kPrinterId);

  auto cups_print_job = std::make_unique<CupsPrintJob>(
      printer, /*job_id=*/0, kTitle, kPagesNumber,
      ::printing::PrintJob::Source::PRINT_PREVIEW, kId, proto::PrintSettings());
  cups_print_job->set_printed_page_number(kPrintedPageNumber);
  cups_print_job->set_state(CupsPrintJob::State::STATE_STARTED);
  return cups_print_job;
}

}  // namespace

TEST(PrintJobInfoMojomConversionsTest, PrintJobProtoToMojom) {
  mojom::PrintJobInfoPtr print_job_mojo =
      printing::print_management::PrintJobProtoToMojom(
          CreatePrintJobInfoProto());

  EXPECT_EQ(kId, print_job_mojo->id);
  EXPECT_EQ(base::UTF8ToUTF16(kTitle), print_job_mojo->title);
  EXPECT_EQ(base::Time::FromJsTime(kJobCreationTime),
            print_job_mojo->creation_time);
  EXPECT_EQ(base::UTF8ToUTF16(kName), print_job_mojo->printer_name);
  EXPECT_EQ(kUri, print_job_mojo->printer_uri.spec());
  EXPECT_EQ(kPagesNumber, print_job_mojo->number_of_pages);
  EXPECT_EQ(mojom::PrintJobCompletionStatus::kPrinted,
            print_job_mojo->completed_info->completion_status);
  EXPECT_EQ(mojom::PrinterErrorCode::kNoError,
            print_job_mojo->completed_info->printer_error_code);
}

TEST(PrintJobInfoMojomConversionsTest, CupsPrintJobToMojom) {
  auto cups_print_job = CreateCupsPrintJob();
  mojom::PrintJobInfoPtr print_job_mojo =
      printing::print_management::CupsPrintJobToMojom(*cups_print_job);

  EXPECT_EQ(cups_print_job->GetUniqueId(), print_job_mojo->id);
  EXPECT_EQ(base::UTF8ToUTF16(kTitle), print_job_mojo->title);
  EXPECT_EQ(cups_print_job->creation_time(), print_job_mojo->creation_time);
  EXPECT_EQ(base::UTF8ToUTF16(kName), print_job_mojo->printer_name);
  EXPECT_EQ(kUri, print_job_mojo->printer_uri.spec());
  EXPECT_EQ(kPagesNumber, print_job_mojo->number_of_pages);
  EXPECT_EQ(kPrintedPageNumber,
            print_job_mojo->active_print_job_info->printed_pages);
  EXPECT_EQ(mojom::ActivePrintJobState::kStarted,
            print_job_mojo->active_print_job_info->active_state);
}

}  // namespace chromeos
