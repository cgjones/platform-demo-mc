/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.gfx;

import java.util.LinkedList;
import java.util.Queue;
import android.content.Context;
import android.graphics.PointF;
import android.os.SystemClock;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View.OnTouchListener;
import org.mozilla.gecko.ui.PanZoomController;
import org.mozilla.gecko.ui.SimpleScaleGestureDetector;
import org.mozilla.gecko.MotionEventWrapper;
import org.mozilla.gecko.Tab;
import org.mozilla.gecko.Tabs;
import org.mozilla.gecko.GeckoAppShell;

public class SimpleGestureListener
    extends GestureDetector.SimpleOnGestureListener
{
    private TouchEventHandler mTouchEventHandler;

    public SimpleGestureListener(TouchEventHandler touchEventHandler) {
        mTouchEventHandler = touchEventHandler;
    }

    @Override
    public void onLongPress(MotionEvent motionEvent) {
        mTouchEventHandler.onLongPress(motionEvent);
    }

    @Override
    public boolean onSingleTapUp(MotionEvent motionEvent) {
        return mTouchEventHandler.onSingleTapUp(motionEvent);
    }

    @Override
    public boolean onSingleTapConfirmed(MotionEvent motionEvent) {
        return mTouchEventHandler.onSingleTapConfirmed(motionEvent);
    }

    @Override
    public boolean onDoubleTap(MotionEvent motionEvent) {
        return mTouchEventHandler.onDoubleTap(motionEvent);
    }
}
