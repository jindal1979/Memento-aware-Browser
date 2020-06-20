// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import static org.chromium.android_webview.test.AwActivityTestRule.CHECK_INTERVAL;

import android.content.Context;
import android.content.ContextWrapper;
import android.os.Build;
import android.os.ResultReceiver;
import android.support.test.InstrumentationRegistry;
import android.util.Pair;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;

import androidx.test.filters.LargeTest;
import androidx.test.filters.SmallTest;

import org.junit.After;
import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.android_webview.AwContents;
import org.chromium.android_webview.gfx.AwGLFunctor;
import org.chromium.android_webview.test.AwActivityTestRule.TestDependencyFactory;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.MinAndroidSdkLevel;
import org.chromium.content_public.browser.ImeAdapter;
import org.chromium.content_public.browser.WebContentsAccessibility;
import org.chromium.content_public.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.content_public.common.ContentUrlConstants;

import java.lang.ref.Reference;
import java.util.concurrent.Callable;

/**
 * AwContents garbage collection tests. Most apps relies on WebView being
 * garbage collected to release memory. These tests ensure that nothing
 * accidentally prevents AwContents from garbage collected, leading to leaks.
 * See crbug.com/544098 for why @DisableHardwareAccelerationForTest is needed.
 * These tests are flaky on non-cq L bot (crbug.com/1085101) because the trick
 * to remove InputMethodManager reference does not work on L. This is ok as
 * long as these tests are running and passing on some cq bot, so just
 * disabling them on L.
 */
@MinAndroidSdkLevel(Build.VERSION_CODES.M)
@RunWith(AwJUnit4ClassRunner.class)
public class AwContentsGarbageCollectionTest {
    @Rule
    public AwActivityTestRule mActivityTestRule = new AwActivityTestRule() {
        @Override
        public TestDependencyFactory createTestDependencyFactory() {
            if (mOverridenFactory == null) {
                return new TestDependencyFactory();
            } else {
                return mOverridenFactory;
            }
        }
    };

    private TestDependencyFactory mOverridenFactory;

    @After
    public void tearDown() {
        mOverridenFactory = null;
    }

    private static class StrongRefTestContext extends ContextWrapper {
        private AwContents mAwContents;
        public void setAwContentsStrongRef(AwContents awContents) {
            mAwContents = awContents;
        }

        public StrongRefTestContext(Context context) {
            super(context);
        }
    }

    private static class GcTestDependencyFactory extends TestDependencyFactory {
        private final StrongRefTestContext mContext;

        public GcTestDependencyFactory(StrongRefTestContext context) {
            mContext = context;
        }

        @Override
        public AwTestContainerView createAwTestContainerView(
                AwTestRunnerActivity activity, boolean allowHardwareAcceleration) {
            if (activity != mContext.getBaseContext()) Assert.fail();
            return new AwTestContainerView(mContext, allowHardwareAcceleration);
        }
    }

    private static class StrongRefTestAwContentsClient extends TestAwContentsClient {
        private AwContents mAwContentsStrongRef;
        public void setAwContentsStrongRef(AwContents awContents) {
            mAwContentsStrongRef = awContents;
        }
    }

    @Test
    @DisableHardwareAccelerationForTest
    @SmallTest
    @Feature({"AndroidWebView"})
    public void testCreateAndGcOneTime() throws Throwable {
        runAwContentsGcTest(() -> {
            TestAwContentsClient client = new TestAwContentsClient();
            AwTestContainerView containerView =
                    mActivityTestRule.createAwTestContainerViewOnMainSync(client);
            mActivityTestRule.loadUrlAsync(
                    containerView.getAwContents(), ContentUrlConstants.ABOUT_BLANK_DISPLAY_URL);
            containerView = null;
            return null;
        });
    }

    @Test
    @DisableHardwareAccelerationForTest
    @SmallTest
    @Feature({"AndroidWebView"})
    public void testHoldKeyboardResultReceiver() throws Throwable {
        runAwContentsGcTest(() -> {
            TestAwContentsClient client = new TestAwContentsClient();
            AwTestContainerView containerView =
                    mActivityTestRule.createAwTestContainerViewOnMainSync(client);
            mActivityTestRule.loadUrlAsync(
                    containerView.getAwContents(), ContentUrlConstants.ABOUT_BLANK_DISPLAY_URL);
            // When we call showSoftInput(), we pass a ResultReceiver object as a parameter.
            // Android framework will hold the object reference until the matching
            // ResultReceiver in InputMethodService (IME app) gets garbage-collected.
            // WebView object wouldn't get gc'ed once OSK shows up because of this.
            // It is difficult to show keyboard and wait until input method window shows up.
            // Instead, we simply emulate Android's behavior by keeping strong references.
            // See crbug.com/595613 for details.
            ResultReceiver resultReceiver = TestThreadUtils.runOnUiThreadBlocking(
                    () -> ImeAdapter.fromWebContents(containerView.getWebContents())
                                       .getNewShowKeyboardReceiver());

            return resultReceiver;
        });
    }

    @Test
    @DisableHardwareAccelerationForTest
    @SmallTest
    @Feature({"AndroidWebView"})
    public void testAccessibility() throws Throwable {
        runAwContentsGcTest(() -> {
            TestAwContentsClient client = new TestAwContentsClient();
            AwTestContainerView containerView =
                    mActivityTestRule.createAwTestContainerViewOnMainSync(client);
            mActivityTestRule.loadUrlAsync(
                    containerView.getAwContents(), ContentUrlConstants.ABOUT_BLANK_DISPLAY_URL);
            TestThreadUtils.runOnUiThreadBlocking(() -> {
                WebContentsAccessibility webContentsA11y =
                        WebContentsAccessibility.fromWebContents(containerView.getWebContents());
                webContentsA11y.setState(true);
                // Enable a11y for testing.
                webContentsA11y.setAccessibilityEnabledForTesting();
                // Initialize native object.
                containerView.getAccessibilityNodeProvider();
                Assert.assertTrue(webContentsA11y.isAccessibilityEnabled());
            });

            return null;
        });
    }

    @Test
    @DisableHardwareAccelerationForTest
    @SmallTest
    @Feature({"AndroidWebView"})
    public void testReferenceFromClient() throws Throwable {
        runAwContentsGcTest(() -> {
            StrongRefTestAwContentsClient client = new StrongRefTestAwContentsClient();
            AwTestContainerView containerView =
                    mActivityTestRule.createAwTestContainerViewOnMainSync(client);
            client.setAwContentsStrongRef(containerView.getAwContents());
            mActivityTestRule.loadUrlAsync(
                    containerView.getAwContents(), ContentUrlConstants.ABOUT_BLANK_DISPLAY_URL);

            containerView = null;
            return null;
        });
    }

    @Test
    @DisableHardwareAccelerationForTest
    @SmallTest
    @Feature({"AndroidWebView"})
    public void testReferenceFromContext() throws Throwable {
        runAwContentsGcTest(() -> {
            TestAwContentsClient client = new TestAwContentsClient();
            StrongRefTestContext context =
                    new StrongRefTestContext(mActivityTestRule.getActivity());
            mOverridenFactory = new GcTestDependencyFactory(context);
            AwTestContainerView containerView =
                    mActivityTestRule.createAwTestContainerViewOnMainSync(client);
            mOverridenFactory = null;
            mActivityTestRule.loadUrlAsync(
                    containerView.getAwContents(), ContentUrlConstants.ABOUT_BLANK_DISPLAY_URL);

            containerView = null;
            return null;
        });
    }

    @Test
    @DisableHardwareAccelerationForTest
    @LargeTest
    @Feature({"AndroidWebView"})
    public void testCreateAndGcManyTimes() throws Throwable {
        runAwContentsGcTest(() -> {
            final int concurrentInstances = 4;
            final int repetitions = 16;

            for (int i = 0; i < repetitions; ++i) {
                for (int j = 0; j < concurrentInstances; ++j) {
                    StrongRefTestAwContentsClient client = new StrongRefTestAwContentsClient();
                    StrongRefTestContext context =
                            new StrongRefTestContext(mActivityTestRule.getActivity());
                    mOverridenFactory = new GcTestDependencyFactory(context);
                    AwTestContainerView view =
                            mActivityTestRule.createAwTestContainerViewOnMainSync(client);
                    mOverridenFactory = null;
                    // Embedding app can hold onto a strong ref to the WebView from either
                    // WebViewClient or WebChromeClient. That should not prevent WebView from
                    // gc-ed. We simulate that behavior by making the equivalent change here,
                    // have AwContentsClient hold a strong ref to the AwContents object.
                    client.setAwContentsStrongRef(view.getAwContents());
                    context.setAwContentsStrongRef(view.getAwContents());
                    mActivityTestRule.loadUrlAsync(
                            view.getAwContents(), ContentUrlConstants.ABOUT_BLANK_DISPLAY_URL);
                }
                Assert.assertTrue(AwContents.getNativeInstanceCount() >= concurrentInstances);
                Assert.assertTrue(
                        AwContents.getNativeInstanceCount() <= (i + 1) * concurrentInstances);
                removeAllViews();
            }
            return null;
        });
    }

    @Test
    @DisableHardwareAccelerationForTest
    @SmallTest
    @Feature({"AndroidWebView"})
    public void testGcAfterUsingJavascriptObject() throws Throwable {
        runAwContentsGcTest(() -> {
            // Javascript object with a reference to WebView.
            class Test {
                Test(int value, AwContents awContents) {
                    mValue = value;
                    mAwContents = awContents;
                }
                @JavascriptInterface
                public int getValue() {
                    return mValue;
                }
                public AwContents getAwContents() {
                    return mAwContents;
                }
                private int mValue;
                private AwContents mAwContents;
            }
            String html = "<html>Hello World</html>";
            TestAwContentsClient contentsClient = new TestAwContentsClient();
            AwTestContainerView containerView =
                    mActivityTestRule.createAwTestContainerViewOnMainSync(contentsClient);
            AwActivityTestRule.enableJavaScriptOnUiThread(containerView.getAwContents());
            final AwContents awContents = containerView.getAwContents();
            final Test jsObject = new Test(42, awContents);
            AwActivityTestRule.addJavascriptInterfaceOnUiThread(awContents, jsObject, "test");
            mActivityTestRule.loadDataSync(
                    awContents, contentsClient.getOnPageFinishedHelper(), html, "text/html", false);
            Assert.assertEquals(String.valueOf(42),
                    mActivityTestRule.executeJavaScriptAndWaitForResult(
                            awContents, contentsClient, "test.getValue()"));

            containerView = null;
            return null;
        });
    }

    // This moves the test body that manipulates AwContents and such objects into
    // a stack frame that's guaranteed to be cleared when the gc checks are run.
    // Otherwise the thread may hold local references (ie from stack variables)
    // to objects.
    private void runAwContentsGcTest(Callable<Object> setup) throws Exception {
        gcAndCheckAllAwContentsDestroyed();
        Object heldObject = setup.call();
        try {
            removeAllViews();

            // This clears a reference that InputMethodManager holds onto focused view.
            TestThreadUtils.runOnUiThreadBlocking(() -> {
                Window window = mActivityTestRule.getActivity().getWindow();
                window.addFlags(WindowManager.LayoutParams.FLAG_LOCAL_FOCUS_MODE);
                window.setLocalFocus(false, false);
                window.clearFlags(WindowManager.LayoutParams.FLAG_LOCAL_FOCUS_MODE);
            });

            gcAndCheckAllAwContentsDestroyed();
        } finally {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                Reference.reachabilityFence(heldObject);
            }
        }
    }

    private void removeAllViews() {
        InstrumentationRegistry.getInstrumentation().runOnMainSync(
                () -> mActivityTestRule.getActivity().removeAllViews());
    }

    private void gcAndCheckAllAwContentsDestroyed() {
        Runtime.getRuntime().gc();

        Runnable criteria = () -> {
            Pair<Integer, Integer> nativeCounts = null;
            try {
                nativeCounts = TestThreadUtils.runOnUiThreadBlocking(() -> {
                    return Pair.create(AwContents.getNativeInstanceCount(),
                            AwGLFunctor.getNativeInstanceCount());
                });
            } catch (Exception e) {
                Assert.fail(e.toString());
            }
            Assert.assertEquals("AwContents count", (int) nativeCounts.first, 0);
            Assert.assertEquals("AwGLFunctor count", (int) nativeCounts.second, 0);
        };

        // Depending on a single gc call can make this test flaky. It's possible
        // that the WebView still has transient references during load so it does not get
        // gc-ed in the one gc-call above. Instead call gc again if exit criteria fails to
        // catch this case.
        final long timeoutBetweenGcMs = 1000L;
        for (int i = 0; i < 15; ++i) {
            try {
                CriteriaHelper.pollInstrumentationThread(
                        criteria, timeoutBetweenGcMs, CHECK_INTERVAL);
                break;
            } catch (AssertionError e) {
                Runtime.getRuntime().gc();
            }
        }

        // Ensure it passes w/o Assertions.
        criteria.run();
    }
}
