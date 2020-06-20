// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test.services;

import static org.chromium.android_webview.test.OnlyRunIn.ProcessMode.SINGLE_PROCESS;

import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

import androidx.test.filters.MediumTest;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.android_webview.common.services.IMetricsBridgeService;
import org.chromium.android_webview.proto.MetricsBridgeRecords.HistogramRecord;
import org.chromium.android_webview.proto.MetricsBridgeRecords.HistogramRecord.RecordType;
import org.chromium.android_webview.services.MetricsBridgeService;
import org.chromium.android_webview.test.AwActivityTestRule;
import org.chromium.android_webview.test.AwJUnit4ClassRunner;
import org.chromium.android_webview.test.OnlyRunIn;
import org.chromium.base.ContextUtils;
import org.chromium.base.FileUtils;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.List;
import java.util.concurrent.FutureTask;

/**
 * Test MetricsBridgeService.
 */
@RunWith(AwJUnit4ClassRunner.class)
@OnlyRunIn(SINGLE_PROCESS)
public class MetricsBridgeServiceTest {
    private static final byte[] PARSING_LOG_RESULT_SUCCESS_RECORD =
            HistogramRecord.newBuilder()
                    .setRecordType(RecordType.HISTOGRAM_LINEAR)
                    .setHistogramName("Android.WebView.NonEmbeddedMetrics.ParsingLogResult")
                    .setSample(MetricsBridgeService.ParsingLogResult.SUCCESS)
                    .setMin(1)
                    .setMax(MetricsBridgeService.ParsingLogResult.COUNT)
                    .setNumBuckets(MetricsBridgeService.ParsingLogResult.COUNT + 1)
                    .build()
                    .toByteArray();

    private static final byte[] RETRIEVE_METRICS_TASK_STATUS_SUCCESS_RECORD =
            HistogramRecord.newBuilder()
                    .setRecordType(RecordType.HISTOGRAM_LINEAR)
                    .setHistogramName(
                            "Android.WebView.NonEmbeddedMetrics.RetrieveMetricsTaskStatus")
                    .setSample(MetricsBridgeService.RetrieveMetricsTaskStatus.SUCCESS)
                    .setMin(1)
                    .setMax(MetricsBridgeService.RetrieveMetricsTaskStatus.COUNT)
                    .setNumBuckets(MetricsBridgeService.RetrieveMetricsTaskStatus.COUNT + 1)
                    .build()
                    .toByteArray();

    private File mTempFile;

    @Before
    public void setUp() throws IOException {
        mTempFile = File.createTempFile("test_webview_metrics_bridge_logs", null);
    }

    @After
    public void tearDown() {
        if (mTempFile.exists()) {
            Assert.assertTrue("Failed to delete \"" + mTempFile + "\"", mTempFile.delete());
        }
    }

    @Test
    @MediumTest
    // Test that the service saves metrics records to file
    public void testSaveToFile() throws Throwable {
        HistogramRecord recordBooleanProto = HistogramRecord.newBuilder()
                                                     .setRecordType(RecordType.HISTOGRAM_BOOLEAN)
                                                     .setHistogramName("testSaveToFile.boolean")
                                                     .setSample(1)
                                                     .build();
        HistogramRecord recordLinearProto = HistogramRecord.newBuilder()
                                                    .setRecordType(RecordType.HISTOGRAM_LINEAR)
                                                    .setHistogramName("testSaveToFile.linear")
                                                    .setSample(123)
                                                    .setMin(1)
                                                    .setMax(1000)
                                                    .setNumBuckets(50)
                                                    .build();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeRecordsToStream(out, recordBooleanProto, recordLinearProto, recordBooleanProto);
        byte[] expectedData = out.toByteArray();

        // Cannot bind to service using real connection since we need to inject test file name.
        MetricsBridgeService service = new MetricsBridgeService(mTempFile);
        // Simulate starting the service by calling onCreate()
        service.onCreate();

        IBinder binder = service.onBind(null);
        IMetricsBridgeService stub = IMetricsBridgeService.Stub.asInterface(binder);
        stub.recordMetrics(recordBooleanProto.toByteArray());
        stub.recordMetrics(recordLinearProto.toByteArray());
        stub.recordMetrics(recordBooleanProto.toByteArray());

        // Block until all tasks are finished to make sure all records are written to file.
        FutureTask<Object> blockTask = service.addTaskToBlock();
        AwActivityTestRule.waitForFuture(blockTask);

        byte[] resultData = FileUtils.readStream(new FileInputStream(mTempFile));
        Assert.assertArrayEquals(
                "byte data from file is different from the expected proto byte data", expectedData,
                resultData);
    }

    @Test
    @MediumTest
    // Test that service recovers saved data from file, appends new records to it and
    // clears the file after a retrieve call.
    public void testRetrieveFromFile() throws Throwable {
        HistogramRecord recordBooleanProto =
                HistogramRecord.newBuilder()
                        .setRecordType(RecordType.HISTOGRAM_BOOLEAN)
                        .setHistogramName("testRecoverFromFile.boolean")
                        .setSample(1)
                        .build();
        HistogramRecord recordLinearProto = HistogramRecord.newBuilder()
                                                    .setRecordType(RecordType.HISTOGRAM_LINEAR)
                                                    .setHistogramName("testRecoverFromFile.linear")
                                                    .setSample(123)
                                                    .setMin(1)
                                                    .setMax(1000)
                                                    .setNumBuckets(50)
                                                    .build();
        // write Initial proto data To File
        writeRecordsToStream(new FileOutputStream(mTempFile), recordBooleanProto, recordLinearProto,
                recordBooleanProto);

        // Cannot bind to service using real connection since we need to inject test file name.
        MetricsBridgeService service = new MetricsBridgeService(mTempFile);
        // Simulate starting the service by calling onCreate()
        service.onCreate();

        IBinder binder = service.onBind(null);
        IMetricsBridgeService stub = IMetricsBridgeService.Stub.asInterface(binder);
        stub.recordMetrics(recordBooleanProto.toByteArray());
        List<byte[]> retrievedDataList = stub.retrieveNonembeddedMetrics();

        byte[][] expectedData = new byte[][] {recordBooleanProto.toByteArray(),
                recordLinearProto.toByteArray(), recordBooleanProto.toByteArray(),
                PARSING_LOG_RESULT_SUCCESS_RECORD, recordBooleanProto.toByteArray(),
                RETRIEVE_METRICS_TASK_STATUS_SUCCESS_RECORD};

        // Assert file is deleted after the retrieve call
        Assert.assertFalse(
                "file should be deleted after retrieve metrics call", mTempFile.exists());
        Assert.assertNotNull("retrieved byte data from the service is null", retrievedDataList);
        Assert.assertArrayEquals("retrieved byte data is different from the expected data",
                expectedData, retrievedDataList.toArray());
    }

    @Test
    @MediumTest
    // Test sending data to the service and retrieving it back.
    public void testRecordAndRetrieveNonembeddedMetrics() throws Throwable {
        HistogramRecord recordProto =
                HistogramRecord.newBuilder()
                        .setRecordType(RecordType.HISTOGRAM_BOOLEAN)
                        .setHistogramName("testRecordAndRetrieveNonembeddedMetrics")
                        .setSample(1)
                        .build();
        byte[][] expectedData = new byte[][] {
                recordProto.toByteArray(), RETRIEVE_METRICS_TASK_STATUS_SUCCESS_RECORD};

        Intent intent =
                new Intent(ContextUtils.getApplicationContext(), MetricsBridgeService.class);
        try (ServiceConnectionHelper helper =
                        new ServiceConnectionHelper(intent, Context.BIND_AUTO_CREATE)) {
            IMetricsBridgeService service =
                    IMetricsBridgeService.Stub.asInterface(helper.getBinder());
            service.recordMetrics(recordProto.toByteArray());
            List<byte[]> retrievedDataList = service.retrieveNonembeddedMetrics();

            Assert.assertArrayEquals("retrieved byte data is different from the expected data",
                    expectedData, retrievedDataList.toArray());
        }
    }

    @Test
    @MediumTest
    // Test sending data to the service and retrieving it back and make sure it's cleared.
    public void testClearAfterRetrieveNonembeddedMetrics() throws Throwable {
        HistogramRecord recordProto =
                HistogramRecord.newBuilder()
                        .setRecordType(RecordType.HISTOGRAM_BOOLEAN)
                        .setHistogramName("testClearAfterRetrieveNonembeddedMetrics")
                        .setSample(1)
                        .build();
        byte[][] expectedData = new byte[][] {
                recordProto.toByteArray(), RETRIEVE_METRICS_TASK_STATUS_SUCCESS_RECORD};

        Intent intent =
                new Intent(ContextUtils.getApplicationContext(), MetricsBridgeService.class);
        try (ServiceConnectionHelper helper =
                        new ServiceConnectionHelper(intent, Context.BIND_AUTO_CREATE)) {
            IMetricsBridgeService service =
                    IMetricsBridgeService.Stub.asInterface(helper.getBinder());
            service.recordMetrics(recordProto.toByteArray());
            List<byte[]> retrievedDataList = service.retrieveNonembeddedMetrics();

            Assert.assertNotNull("retrieved byte data from the service is null", retrievedDataList);
            Assert.assertArrayEquals("retrieved byte data is different from the expected data",
                    expectedData, retrievedDataList.toArray());

            // Retrieve data a second time to make sure it has been cleared after the first call
            retrievedDataList = service.retrieveNonembeddedMetrics();

            Assert.assertArrayEquals("metrics kept by the service hasn't been cleared",
                    new byte[][] {RETRIEVE_METRICS_TASK_STATUS_SUCCESS_RECORD},
                    retrievedDataList.toArray());
        }
    }

    private static void writeRecordsToStream(OutputStream os, HistogramRecord... records)
            throws IOException {
        for (HistogramRecord record : records) {
            record.writeDelimitedTo(os);
        }
        os.close();
    }
}