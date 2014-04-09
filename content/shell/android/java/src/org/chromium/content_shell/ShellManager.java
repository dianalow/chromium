// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_shell;

import android.app.Activity;
import android.content.Context;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.FrameLayout;

import org.chromium.base.CalledByNative;
import org.chromium.base.CommandLine;
import org.chromium.base.JNINamespace;
import org.chromium.base.ThreadUtils;
import org.chromium.content.browser.ActivityContentVideoViewClient;
import org.chromium.content.browser.ContentVideoViewClient;
import org.chromium.content.browser.ContentView;
import org.chromium.content.browser.ContentViewClient;
import org.chromium.content.browser.ContentViewRenderView;
import org.chromium.content.common.ContentSwitches;
import org.chromium.ui.base.WindowAndroid;

/**
 * Container and generator of ShellViews.
 */
@JNINamespace("content")
public class ShellManager extends FrameLayout {

    public static final String DEFAULT_SHELL_URL = "http://www.google.com";
    private static boolean sStartup = true;
    private WindowAndroid mWindow;
    private Shell mActiveShell;

    private String mStartupUrl = DEFAULT_SHELL_URL;

    // The target for all content rendering.
    private ContentViewRenderView mContentViewRenderView;
    private ContentViewClient mContentViewClient;

    /**
     * Constructor for inflating via XML.
     */
    public ShellManager(final Context context, AttributeSet attrs) {
        super(context, attrs);
        nativeInit(this);
        mContentViewClient = new ContentViewClient() {
            @Override
            public ContentVideoViewClient getContentVideoViewClient() {
                return new ActivityContentVideoViewClient((Activity) context) {
                    @Override
                    public void onShowCustomView(View view) {
                        super.onShowCustomView(view);
                        if (!CommandLine.getInstance().hasSwitch(
                                ContentSwitches.DISABLE_OVERLAY_FULLSCREEN_VIDEO_SUBTITLE)) {
                            setOverlayVideoMode(true);
                        }
                    }

                    @Override
                    public void onDestroyContentVideoView() {
                        super.onDestroyContentVideoView();
                        if (!CommandLine.getInstance().hasSwitch(
                                ContentSwitches.DISABLE_OVERLAY_FULLSCREEN_VIDEO_SUBTITLE)) {
                            setOverlayVideoMode(false);
                        }
                    }
                };
            }
        };
    }

    /**
     * @param window The window used to generate all shells.
     */
    public void setWindow(WindowAndroid window) {
        assert window != null;
        mWindow = window;
        mContentViewRenderView = new ContentViewRenderView(getContext(), window) {
            @Override
            protected void onReadyToRender() {
                if (sStartup) {
                    mActiveShell.loadUrl(mStartupUrl);
                    sStartup = false;
                }
            }
        };
    }

    /**
     * @return The window used to generate all shells.
     */
    public WindowAndroid getWindow() {
        return mWindow;
    }

    /**
     * Sets the startup URL for new shell windows.
     */
    public void setStartupUrl(String url) {
        mStartupUrl = url;
    }

    /**
     * @return The currently visible shell view or null if one is not showing.
     */
    public Shell getActiveShell() {
        return mActiveShell;
    }

    /**
     * Creates a new shell pointing to the specified URL.
     * @param url The URL the shell should load upon creation.
     */
    public void launchShell(String url) {
        ThreadUtils.assertOnUiThread();
        Shell previousShell = mActiveShell;
        nativeLaunchShell(url);
        if (previousShell != null) previousShell.close();
    }

    /**
     * Enter or leave overlay video mode.
     * @param enabled Whether overlay mode is enabled.
     */
    public void setOverlayVideoMode(boolean enabled) {
        if (mContentViewRenderView == null) return;
        mContentViewRenderView.setOverlayVideoMode(enabled);
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private Object createShell(long nativeShellPtr) {
        assert mContentViewRenderView != null;
        LayoutInflater inflater =
                (LayoutInflater) getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        Shell shellView = (Shell) inflater.inflate(R.layout.shell_view, null);
        shellView.initialize(nativeShellPtr, mWindow, mContentViewClient);

        // TODO(tedchoc): Allow switching back to these inactive shells.
        if (mActiveShell != null) removeShell(mActiveShell);

        showShell(shellView);
        return shellView;
    }

    private void showShell(Shell shellView) {
        shellView.setContentViewRenderView(mContentViewRenderView);
        addView(shellView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        mActiveShell = shellView;
        ContentView contentView = mActiveShell.getContentView();
        if (contentView != null) {
            mContentViewRenderView.setCurrentContentViewCore(contentView.getContentViewCore());
            contentView.getContentViewCore().onShow();
        }
    }

    @CalledByNative
    private void removeShell(Shell shellView) {
        if (shellView == mActiveShell) mActiveShell = null;
        if (shellView.getParent() == null) return;
        ContentView contentView = shellView.getContentView();
        if (contentView != null) contentView.onHide();
        shellView.setContentViewRenderView(null);
        removeView(shellView);
    }

    private static native void nativeInit(Object shellManagerInstance);
    private static native void nativeLaunchShell(String url);
}
