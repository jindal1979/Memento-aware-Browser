// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import static org.mockito.Mockito.when;
import static org.mockito.MockitoAnnotations.initMocks;

import static org.chromium.android_webview.test.OnlyRunIn.ProcessMode.SINGLE_PROCESS;

import android.annotation.SuppressLint;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobWorkItem;
import android.content.ComponentName;
import android.content.Context;
import android.os.PersistableBundle;

import androidx.test.filters.MediumTest;
import androidx.test.filters.SmallTest;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;

import org.chromium.android_webview.common.AwSwitches;
import org.chromium.android_webview.common.variations.VariationsServiceMetricsHelper;
import org.chromium.android_webview.common.variations.VariationsUtils;
import org.chromium.android_webview.services.AwVariationsSeedFetcher;
import org.chromium.android_webview.test.util.VariationsTestUtils;
import org.chromium.base.ContextUtils;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.components.background_task_scheduler.TaskIds;
import org.chromium.components.variations.firstrun.VariationsSeedFetcher;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/**
 * Test AwVariationsSeedFetcher.
 */
@RunWith(AwJUnit4ClassRunner.class)
@OnlyRunIn(SINGLE_PROCESS)
public class AwVariationsSeedFetcherTest {
    private static final int HTTP_NOT_FOUND = 404;
    private static final int JOB_ID = TaskIds.WEBVIEW_VARIATIONS_SEED_FETCH_JOB_ID;
    private static final long DOWNLOAD_DURATION = 10;
    private static final long JOB_DELAY = 2000;
    private static final long START_TIME = 100;

    // A test JobScheduler which only holds one job, and never does anything with it.
    private class TestJobScheduler extends JobScheduler {
        public JobInfo mJob;

        public void clear() {
            mJob = null;
        }

        public void assertScheduled() {
            Assert.assertNotNull("No job scheduled", mJob);
        }

        public void assertNotScheduled() {
            Assert.assertNull("Job should not have been scheduled", mJob);
        }

        @Override
        public void cancel(int jobId) {
            throw new UnsupportedOperationException();
        }

        @Override
        public void cancelAll() {
            throw new UnsupportedOperationException();
        }

        @Override
        public int enqueue(JobInfo job, JobWorkItem work) {
            throw new UnsupportedOperationException();
        }

        @Override
        public List<JobInfo> getAllPendingJobs() {
            ArrayList<JobInfo> list = new ArrayList<>();
            if (mJob != null) list.add(mJob);
            return list;
        }

        @Override
        public JobInfo getPendingJob(int jobId) {
            if (mJob != null && mJob.getId() == jobId) return mJob;
            return null;
        }

        @Override
        public int schedule(JobInfo job) {
            Assert.assertEquals("Job scheduled with wrong ID", JOB_ID, job.getId());
            Assert.assertEquals("Job scheduled with wrong network type",
                    JobInfo.NETWORK_TYPE_ANY, job.getNetworkType());
            Assert.assertTrue("Job scheduled without charging requirement",
                    job.isRequireCharging());
            mJob = job;
            return JobScheduler.RESULT_SUCCESS;
        }
    }

    // A test VariationsSeedFetcher which doesn't actually download seeds, but verifies the request
    // parameters.
    private class TestVariationsSeedFetcher extends VariationsSeedFetcher {
        public int fetchResult;

        @Override
        public SeedFetchInfo downloadContent(@VariationsSeedFetcher.VariationsPlatform int platform,
                String restrictMode, String milestone, String channel) {
            Assert.assertEquals(VariationsSeedFetcher.VariationsPlatform.ANDROID_WEBVIEW, platform);
            Assert.assertTrue(Integer.parseInt(milestone) > 0);
            mClock.timestamp += DOWNLOAD_DURATION;

            SeedFetchInfo fetchInfo = new SeedFetchInfo();
            fetchInfo.seedFetchResult = fetchResult;
            return fetchInfo;
        }
    }

    // A test VariationsSeedFetcher that fails all seed requests.
    private class FailingVariationsSeedFetcher extends VariationsSeedFetcher {
        @Override
        public SeedFetchInfo downloadContent(@VariationsSeedFetcher.VariationsPlatform int platform,
                String restrictMode, String milestone, String channel) {
            SeedFetchInfo fetchInfo = new SeedFetchInfo();
            fetchInfo.seedFetchResult = -1;
            return fetchInfo;
        }
    }

    // A fake clock instance with a controllable timestamp.
    private class TestClock implements AwVariationsSeedFetcher.Clock {
        public long timestamp;

        @Override
        public long currentTimeMillis() {
            return timestamp;
        }
    }

    // A test AwVariationsSeedFetcher that doesn't call JobFinished.
    private class TestAwVariationsSeedFetcher extends AwVariationsSeedFetcher {
        public CallbackHelper helper = new CallbackHelper();
        private JobParameters mJobParameters;
        private boolean mNeededReschedule;

        public JobParameters getFinishedJobParameters() {
            return mJobParameters;
        }

        public boolean neededReschedule() {
            return mNeededReschedule;
        }

        // p is null in this test. Don't actually call JobService.jobFinished.
        @Override
        protected void onFinished(JobParameters jobParameters, boolean needsReschedule) {
            mJobParameters = jobParameters;
            mNeededReschedule = needsReschedule;
            helper.notifyCalled();
        }
    }

    private TestJobScheduler mScheduler = new TestJobScheduler();
    private TestVariationsSeedFetcher mDownloader = new TestVariationsSeedFetcher();
    private TestClock mClock = new TestClock();
    private Context mContext;

    @Mock
    private JobParameters mMockJobParameters;

    @Before
    public void setUp() throws IOException {
        AwVariationsSeedFetcher.setMocks(mScheduler, mDownloader);
        VariationsTestUtils.deleteSeeds();
        mContext = ContextUtils.getApplicationContext();
        initMocks(this);
    }

    @After
    public void tearDown() throws IOException {
        AwVariationsSeedFetcher.setMocks(null, null);
        AwVariationsSeedFetcher.setTestClock(null);
        VariationsTestUtils.deleteSeeds();
    }

    // Test scheduleIfNeeded(), which should schedule a job.
    @Test
    @SmallTest
    public void testScheduleWithNoStamp() {
        try {
            AwVariationsSeedFetcher.scheduleIfNeeded();
            mScheduler.assertScheduled();
        } finally {
            mScheduler.clear();
        }
    }

    // Create a stamp file with time = epoch, indicating the download job hasn't run in a long time.
    // Then test scheduleIfNeeded(), which should schedule a job.
    @Test
    @MediumTest
    public void testScheduleWithExpiredStamp() throws IOException {
        File stamp = VariationsUtils.getStampFile();
        try {
            Assert.assertFalse("Stamp file already exists", stamp.exists());
            Assert.assertTrue("Failed to create stamp file", stamp.createNewFile());
            Assert.assertTrue("Failed to set stamp time", stamp.setLastModified(0));
            AwVariationsSeedFetcher.scheduleIfNeeded();
            mScheduler.assertScheduled();
        } finally {
            mScheduler.clear();
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }

    // Create a stamp file with time = now, indicating the download job ran recently. Then test
    // scheduleIfNeeded(), which should not schedule a job.
    @Test
    @MediumTest
    public void testScheduleWithFreshStamp() throws IOException {
        File stamp = VariationsUtils.getStampFile();
        try {
            Assert.assertFalse("Stamp file already exists", stamp.exists());
            Assert.assertTrue("Failed to create stamp file", stamp.createNewFile());
            AwVariationsSeedFetcher.scheduleIfNeeded();
            mScheduler.assertNotScheduled();
        } finally {
            mScheduler.clear();
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }

    // Pretend that a job is already scheduled. Then test scheduleIfNeeded(), which should not
    // schedule a job.
    @Test
    @SmallTest
    public void testScheduleAlreadyScheduled() {
        File stamp = VariationsUtils.getStampFile();
        try {
            @SuppressLint("JobSchedulerService")
            ComponentName component = new ComponentName(
                    ContextUtils.getApplicationContext(), AwVariationsSeedFetcher.class);
            JobInfo job = new JobInfo.Builder(JOB_ID, component)
                    .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
                    .setRequiresCharging(true)
                    .build();
            mScheduler.schedule(job);
            AwVariationsSeedFetcher.scheduleIfNeeded();
            // Check that our job object hasn't been replaced (meaning that scheduleIfNeeded didn't
            // schedule a job).
            Assert.assertSame(job, mScheduler.getPendingJob(JOB_ID));
        } finally {
            mScheduler.clear();
        }
    }

    // Tests that the --finch-seed-min-download-period flag can override the job throttling.
    @Test
    @SmallTest
    @CommandLineFlags.Add(AwSwitches.FINCH_SEED_MIN_DOWNLOAD_PERIOD + "=0")
    public void testFinchSeedMinDownloadPeriodFlag() throws IOException {
        File stamp = VariationsUtils.getStampFile();
        try {
            // Create a recent stamp file that would usually prevent job scheduling.
            Assert.assertFalse("Stamp file already exists", stamp.exists());
            Assert.assertTrue("Failed to create stamp file", stamp.createNewFile());

            AwVariationsSeedFetcher.scheduleIfNeeded();

            mScheduler.assertScheduled();
        } finally {
            mScheduler.clear();
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }

    // Tests that the --finch-seed-ignore-pending-download flag results in jobs being rescheduled.
    @Test
    @SmallTest
    @CommandLineFlags.Add(AwSwitches.FINCH_SEED_IGNORE_PENDING_DOWNLOAD)
    public void testFinchSeedIgnorePendingDownloadFlag() {
        File stamp = VariationsUtils.getStampFile();
        try {
            AwVariationsSeedFetcher.scheduleIfNeeded();
            JobInfo originalJob = mScheduler.getPendingJob(JOB_ID);
            Assert.assertNotNull("Job should have been scheduled", originalJob);

            AwVariationsSeedFetcher.scheduleIfNeeded();

            // Check that the job got rescheduled.
            JobInfo rescheduledJob = mScheduler.getPendingJob(JOB_ID);
            Assert.assertNotNull("Job should have been rescheduled", rescheduledJob);
            Assert.assertNotSame(
                    "Rescheduled job should not be equal to the originally scheduled job",
                    originalJob, rescheduledJob);
        } finally {
            mScheduler.clear();
        }
    }

    @Test
    @SmallTest
    public void testFetch() throws IOException, TimeoutException {
        try {
            TestAwVariationsSeedFetcher fetcher = new TestAwVariationsSeedFetcher();

            fetcher.onStartJob(null);

            Assert.assertFalse("neededReschedule should be false before making a request",
                    fetcher.neededReschedule());
            fetcher.helper.waitForCallback(
                    "Timeout out waiting for AwVariationsSeedFetcher to call jobFinished",
                    fetcher.helper.getCallCount());
            Assert.assertFalse("neededReschedule should be false after a successful seed request.",
                    fetcher.neededReschedule());
            File stamp = VariationsUtils.getStampFile();
            Assert.assertTrue("AwVariationsSeedFetcher should have updated stamp file " + stamp,
                    stamp.exists());
        } finally {
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }

    @Test
    @MediumTest
    public void testRetryFetch() throws IOException, TimeoutException {
        try {
            AwVariationsSeedFetcher.setMocks(mScheduler, new FailingVariationsSeedFetcher());

            AwVariationsSeedFetcher.scheduleIfNeeded();

            JobInfo originalJob = mScheduler.getPendingJob(JOB_ID);
            Assert.assertNotNull("Job should have been scheduled", originalJob);
            Assert.assertEquals("Initial request count should be 0", 0,
                    originalJob.getExtras().getInt(
                            AwVariationsSeedFetcher.JOB_REQUEST_COUNT_KEY, -1));

            // This TestAwVariationsSeedFetcher instance will delegate its HTTP requests to the
            // failing VariationsSeedFetcher that we set as the mock instance above.
            TestAwVariationsSeedFetcher fetcher = new TestAwVariationsSeedFetcher();
            when(mMockJobParameters.getExtras()).thenAnswer(invocation -> {
                return mScheduler.getPendingJob(JOB_ID).getExtras();
            });

            fetcher.onStartJob(mMockJobParameters);

            fetcher.helper.waitForCallback(
                    "Timeout out waiting for AwVariationsSeedFetcher to call jobFinished",
                    fetcher.helper.getCallCount());
            Assert.assertTrue("neededReschedule should be true after a failed seed request.",
                    fetcher.neededReschedule());
            int requestCount = fetcher.getFinishedJobParameters().getExtras().getInt(
                    AwVariationsSeedFetcher.JOB_REQUEST_COUNT_KEY);
            Assert.assertEquals("Request count should have increased", requestCount, 1);

        } finally {
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }

    @Test
    @MediumTest
    public void testNoRetryAfterMaxFailedAttempts() throws IOException, TimeoutException {
        try {
            AwVariationsSeedFetcher.setMocks(mScheduler, new FailingVariationsSeedFetcher());

            // This TestAwVariationsSeedFetcher instance will delegate its HTTP requests to the
            // failing VariationsSeedFetcher that we set as the mock instance above.
            TestAwVariationsSeedFetcher fetcher = new TestAwVariationsSeedFetcher();
            PersistableBundle jobInfoExtras = new PersistableBundle();
            jobInfoExtras.putInt(AwVariationsSeedFetcher.JOB_REQUEST_COUNT_KEY,
                    AwVariationsSeedFetcher.JOB_MAX_REQUEST_COUNT);
            when(mMockJobParameters.getExtras()).thenReturn(jobInfoExtras);

            fetcher.onStartJob(mMockJobParameters);

            fetcher.helper.waitForCallback(
                    "Timeout out waiting for AwVariationsSeedFetcher to call jobFinished",
                    fetcher.helper.getCallCount());
            Assert.assertFalse("neededReschedule should be false after the max "
                            + "failed seed requests has been reached.",
                    fetcher.neededReschedule());

        } finally {
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }

    // Tests that metrics are written to SharedPreferences when there's no previous data.
    @Test
    @MediumTest
    public void testMetricsWrittenToPrefsWithoutPreviousData()
            throws IOException, TimeoutException {
        try {
            AwVariationsSeedFetcher.setTestClock(mClock);
            mClock.timestamp = START_TIME;
            mDownloader.fetchResult = HTTP_NOT_FOUND;

            TestAwVariationsSeedFetcher fetcher = new TestAwVariationsSeedFetcher();
            fetcher.onStartJob(null);
            fetcher.helper.waitForCallback(
                    "Timeout out waiting for AwVariationsSeedFetcher to call downloadContent", 0);

            VariationsServiceMetricsHelper metrics =
                    VariationsServiceMetricsHelper.fromVariationsSharedPreferences(mContext);
            Assert.assertEquals(HTTP_NOT_FOUND, metrics.getSeedFetchResult());
            Assert.assertEquals(DOWNLOAD_DURATION, metrics.getSeedFetchTime());
            Assert.assertEquals(START_TIME, metrics.getLastJobStartTime());
            Assert.assertFalse(metrics.hasLastEnqueueTime());
            Assert.assertFalse(metrics.hasJobInterval());
            Assert.assertFalse(metrics.hasJobQueueTime());
        } finally {
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }

    // Tests that metrics are written to SharedPreferences when there is information about the
    // previous job scheduling, but not the previous download.
    @Test
    @MediumTest
    public void testMetricsWrittenToPrefsWithPreviousJobData()
            throws IOException, TimeoutException {
        try {
            AwVariationsSeedFetcher.setTestClock(mClock);
            mClock.timestamp = START_TIME;
            mDownloader.fetchResult = HTTP_NOT_FOUND;
            AwVariationsSeedFetcher.scheduleIfNeeded();
            mScheduler.assertScheduled();

            VariationsServiceMetricsHelper initialMetrics =
                    VariationsServiceMetricsHelper.fromVariationsSharedPreferences(mContext);
            Assert.assertEquals(START_TIME, initialMetrics.getLastEnqueueTime());

            mClock.timestamp += JOB_DELAY;
            TestAwVariationsSeedFetcher fetcher = new TestAwVariationsSeedFetcher();
            fetcher.onStartJob(null);
            fetcher.helper.waitForCallback(
                    "Timeout out waiting for AwVariationsSeedFetcher to call downloadContent", 0);

            VariationsServiceMetricsHelper metrics =
                    VariationsServiceMetricsHelper.fromVariationsSharedPreferences(mContext);
            Assert.assertEquals(HTTP_NOT_FOUND, metrics.getSeedFetchResult());
            Assert.assertEquals(DOWNLOAD_DURATION, metrics.getSeedFetchTime());
            Assert.assertEquals(START_TIME + JOB_DELAY, metrics.getLastJobStartTime());
            Assert.assertEquals(JOB_DELAY, metrics.getJobQueueTime());
            Assert.assertFalse(metrics.hasLastEnqueueTime());
            Assert.assertFalse(metrics.hasJobInterval());
        } finally {
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
            mScheduler.clear();
        }
    }

    // Tests that metrics are written to SharedPreferences when there is information about the
    // previous job scheduling and download.
    @Test
    @MediumTest
    public void testMetricsWrittenToPrefsWithPreviousJobAndFetchData()
            throws IOException, TimeoutException {
        long appRunDelay = TimeUnit.DAYS.toMillis(2);
        try {
            AwVariationsSeedFetcher.setTestClock(mClock);
            VariationsServiceMetricsHelper initialMetrics =
                    VariationsServiceMetricsHelper.fromVariationsSharedPreferences(mContext);
            mClock.timestamp = START_TIME;
            mDownloader.fetchResult = HTTP_NOT_FOUND;
            initialMetrics.setLastJobStartTime(mClock.timestamp);
            mClock.timestamp += appRunDelay;
            initialMetrics.setLastEnqueueTime(mClock.timestamp);
            boolean committed = initialMetrics.writeMetricsToVariationsSharedPreferences(mContext);
            Assert.assertTrue("Failed to commit initial variations SharedPreferences", committed);

            mClock.timestamp += JOB_DELAY;
            TestAwVariationsSeedFetcher fetcher = new TestAwVariationsSeedFetcher();
            fetcher.onStartJob(null);
            fetcher.helper.waitForCallback(
                    "Timeout out waiting for AwVariationsSeedFetcher to call downloadContent", 0);

            VariationsServiceMetricsHelper metrics =
                    VariationsServiceMetricsHelper.fromVariationsSharedPreferences(mContext);
            Assert.assertEquals(HTTP_NOT_FOUND, metrics.getSeedFetchResult());
            Assert.assertEquals(DOWNLOAD_DURATION, metrics.getSeedFetchTime());
            Assert.assertEquals(
                    START_TIME + appRunDelay + JOB_DELAY, metrics.getLastJobStartTime());
            Assert.assertEquals(appRunDelay + JOB_DELAY, metrics.getJobInterval());
            Assert.assertEquals(JOB_DELAY, metrics.getJobQueueTime());
            Assert.assertFalse(metrics.hasLastEnqueueTime());
        } finally {
            VariationsTestUtils.deleteSeeds(); // Remove the stamp file.
        }
    }
}
