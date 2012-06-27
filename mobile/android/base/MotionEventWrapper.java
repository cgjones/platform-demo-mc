/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.gfx.DisplayPortMetrics;
import org.mozilla.gecko.gfx.IntSize;
import org.mozilla.gecko.gfx.ViewportMetrics;
import org.mozilla.gecko.gfx.LayerController;
import android.os.*;
import android.app.*;
import android.view.*;
import android.content.*;
import android.graphics.*;
import android.widget.*;
import android.hardware.*;
import android.location.*;
import android.util.FloatMath;
import android.util.DisplayMetrics;
import android.graphics.PointF;
import android.text.format.Time;
import android.os.SystemClock;
import java.lang.Math;
import java.lang.System;

import android.util.Log;

/**
 * XXX: Possibly refactor some stuff in here.
 *
 * Mostly duplicated code from GeckoEvent, without all the cruft.
 */
public class MotionEventWrapper {
    public int mAction;
    public int mCount;
    public long mTime;
    public int mMetaState;
    public Point[] mPoints;
    public int[] mPointIndicies;
    public int mPointerIndex;
    public float[] mOrientations;
    public float[] mPressures;
    public Point[] mPointRadii;

    public MotionEventWrapper(MotionEvent m) {
        mAction = m.getAction();
        mTime = (System.currentTimeMillis() - SystemClock.elapsedRealtime()) + m.getEventTime();
        mMetaState = m.getMetaState();

        switch (mAction & MotionEvent.ACTION_MASK) {
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE: {
                mCount = m.getPointerCount();
                mPoints = new Point[mCount];
                mPointIndicies = new int[mCount];
                mOrientations = new float[mCount];
                mPressures = new float[mCount];
                mPointRadii = new Point[mCount];
                mPointerIndex = (mAction & MotionEvent.ACTION_POINTER_INDEX_MASK) >> MotionEvent.ACTION_POINTER_INDEX_SHIFT;
                for (int i = 0; i < mCount; i++) {
                    addMotionPoint(i, i, m);
                }
                break;
            }
            default: {
                mCount = 0;
                mPointerIndex = -1;
                mPoints = new Point[mCount];
                mPointIndicies = new int[mCount];
                mOrientations = new float[mCount];
                mPressures = new float[mCount];
                mPointRadii = new Point[mCount];
            }
        }
    }

    public void addMotionPoint(int index, int eventIndex, MotionEvent event) {
        try {
            final LayerController layerController = GeckoApp.mAppContext.getLayerController();

            Point pointSentToGecko;
            if (layerController.panZoomInGecko()) {
                pointSentToGecko = new Point(Math.round(event.getX(eventIndex)), Math.round(event.getY(eventIndex)));
            } else {
                PointF geckoPoint = new PointF(event.getX(eventIndex), event.getY(eventIndex));
                geckoPoint = GeckoApp.mAppContext.getLayerController().convertViewPointToLayerPoint(geckoPoint);
                pointSentToGecko = new Point(Math.round(geckoPoint.x), Math.round(geckoPoint.y));
            }

            mPoints[index] = pointSentToGecko;
            mPointIndicies[index] = event.getPointerId(eventIndex);
            // getToolMajor, getToolMinor and getOrientation are API Level 9 features
            if (Build.VERSION.SDK_INT >= 9) {
                double radians = event.getOrientation(eventIndex);
                mOrientations[index] = (float) Math.toDegrees(radians);
                // w3c touchevents spec does not allow orientations == 90
                // this shifts it to -90, which will be shifted to zero below
                if (mOrientations[index] == 90)
                    mOrientations[index] = -90;

                // w3c touchevent radius are given by an orientation between 0 and 90
                // the radius is found by removing the orientation and measuring the x and y
                // radius of the resulting ellipse
                // for android orientations >= 0 and < 90, the major axis should correspond to
                // just reporting the y radius as the major one, and x as minor
                // however, for a radius < 0, we have to shift the orientation by adding 90, and
                // reverse which radius is major and minor
                if (mOrientations[index] < 0) {
                    mOrientations[index] += 90;
                    mPointRadii[index] = new Point((int)event.getToolMajor(eventIndex)/2,
                                                   (int)event.getToolMinor(eventIndex)/2);
                } else {
                    mPointRadii[index] = new Point((int)event.getToolMinor(eventIndex)/2,
                                                   (int)event.getToolMajor(eventIndex)/2);
                }
            } else {
                float size = event.getSize(eventIndex);
                DisplayMetrics displaymetrics = new DisplayMetrics();
                GeckoApp.mAppContext.getWindowManager().getDefaultDisplay().getMetrics(displaymetrics);
                size = size*Math.min(displaymetrics.heightPixels, displaymetrics.widthPixels);
                mPointRadii[index] = new Point((int)size,(int)size);
                mOrientations[index] = 0;
            }
            mPressures[index] = event.getPressure(eventIndex);
        } catch(Exception ex) {
            //Log.e(LOGTAG, "Error creating motion point " + index, ex);
            mPointRadii[index] = new Point(0, 0);
            mPoints[index] = new Point(0, 0);
        }
    }
}
