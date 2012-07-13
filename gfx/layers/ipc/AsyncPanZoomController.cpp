/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorParent.h"
#include "mozilla/Util.h"
#include "mozilla/XPCOM.h"
#include "AsyncPanZoomController.h"
#include "nsIThreadManager.h"
#include "nsThreadUtils.h"

#if defined(MOZ_WIDGET_ANDROID)
#include "AndroidBridge.h"
#include <android/log.h>
#endif

namespace mozilla {
namespace layers {

/**
 * Frames for the double tap zoom animation. This sequence looks smoother than
 * simply straight-line zooming it.
 */
float ZOOM_ANIMATION_FRAMES[] = {
  0.00000f,   /* 0 */
  0.10211f,   /* 1 */
  0.19864f,   /* 2 */
  0.29043f,   /* 3 */
  0.37816f,   /* 4 */
  0.46155f,   /* 5 */
  0.54054f,   /* 6 */
  0.61496f,   /* 7 */
  0.68467f,   /* 8 */
  0.74910f,   /* 9 */
  0.80794f,   /* 10 */
  0.86069f,   /* 11 */
  0.90651f,   /* 12 */
  0.94471f,   /* 13 */
  0.97401f,   /* 14 */
  0.99309f,   /* 15 */
};

float AsyncPanZoomController::EPSILON = 0.0001;

PRInt32 AsyncPanZoomController::REPAINT_INTERVAL = 250;

AsyncPanZoomController::AsyncPanZoomController(GeckoContentController* aGeckoContentController)
  :  mState(NOTHING),
     mX(this),
     mY(this),
     mLayersUpdated(false),
     mIsCompositing(false),
     mReentrantMonitor("asyncpanzoomcontroller"),
     mDPI(72),
     mGeckoContentController(aGeckoContentController)
{
  SetDPI(mDPI);
}

AsyncPanZoomController::~AsyncPanZoomController() {

}

nsEventStatus AsyncPanZoomController::HandleInputEvent(const InputEvent& event) {
  nsEventStatus rv = nsEventStatus_eIgnore;
  if (!mIsCompositing)
    return rv;

  switch (event.mMessage) {
  case MULTITOUCH_START_POINTER:
  case MULTITOUCH_START: rv = OnTouchStart((const MultiTouchEvent&)event); break;
  case MULTITOUCH_MOVE: rv = OnTouchMove((const MultiTouchEvent&)event); break;
  case MULTITOUCH_END: rv = OnTouchEnd((const MultiTouchEvent&)event); break;
  case MULTITOUCH_CANCEL: rv = OnTouchCancel((const MultiTouchEvent&)event); break;
  case PINCH_START: rv = OnScaleBegin((const PinchEvent&)event); break;
  case PINCH_SCALE: rv = OnScale((const PinchEvent&)event); break;
  case PINCH_END: rv = OnScaleEnd((const PinchEvent&)event); break;
  case TAP_LONG: rv = OnLongPress((const TapEvent&)event); break;
  case TAP_UP: rv = OnSingleTapUp((const TapEvent&)event); break;
  case TAP_CONFIRMED: rv = OnSingleTapConfirmed((const TapEvent&)event); break;
  case TAP_DOUBLE: rv = OnDoubleTap((const TapEvent&)event); break;
  case TAP_CANCEL: rv = OnCancelTap(); break;
  default: break;
  }

  mLastEventTime = event.mTime;
  return rv;
}

nsEventStatus AsyncPanZoomController::OnTouchStart(const MultiTouchEvent& event) {
  SingleTouchData& touch = GetTouchFromEvent(event);

  nsIntPoint point = touch.mScreenPoint;
  PRInt32 xPos = point.x, yPos = point.y;

  switch (mState) {
    case ANIMATED_ZOOM:
      // force redraw
    case FLING:
    case BOUNCE:
      CancelAnimation();
    case NOTHING:
    case WAITING_LISTENERS:
      mX.StartTouch(xPos);
      mY.StartTouch(yPos);
      mState = TOUCHING;
      break;
    case TOUCHING:
    case PANNING:
    case PANNING_LOCKED:
    case PANNING_HOLD:
    case PANNING_HOLD_LOCKED:
    case PINCHING:
      break;
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnTouchMove(const MultiTouchEvent& event) {
  SingleTouchData& touch = GetTouchFromEvent(event);
  nsIntPoint point = touch.mScreenPoint;
  PRInt32 xPos = point.x, yPos = point.y;

  switch (mState) {
    case ANIMATED_ZOOM:
    case FLING:
    case BOUNCE:
    case NOTHING:
    case WAITING_LISTENERS:
    case TOUCHING:
      if (PanDistance(event) < mPanThreshold) {
        return nsEventStatus_eConsumeNoDefault;
      }
      mLastRepaint = event.mTime;
      mX.StartTouch(xPos);
      mY.StartTouch(yPos);
      OnCancelTap();
      mState = PANNING;
      break;
    case PANNING:
      TrackTouch(event);
      break;
    case PANNING_LOCKED:
    case PANNING_HOLD:
    case PANNING_HOLD_LOCKED:
    case PINCHING:
      break;
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnTouchEnd(const MultiTouchEvent& event) {
  OnCancelTap();

  switch (mState) {
  case FLING:
  case BOUNCE:
  case WAITING_LISTENERS:
  case ANIMATED_ZOOM:
  case NOTHING:
    break;
  case TOUCHING:
    mState = NOTHING;
    break;
  case PANNING:
  case PANNING_LOCKED:
  case PANNING_HOLD:
  case PANNING_HOLD_LOCKED:
    {
      ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
      ForceRepaint();
      SendViewportChange();
    }
    mState = FLING;
    break;
  case PINCHING:
    break;
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnTouchCancel(const MultiTouchEvent& event) {
  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnScaleBegin(const PinchEvent& event) {
  OnCancelTap();
  mState = PINCHING;
  mLastZoomFocus = event.mFocusPoint;

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnScale(const PinchEvent& event) {
  float prevSpan = event.mPreviousSpan;
  if (fabsf(prevSpan) <= EPSILON) {
    // We're still handling it; we've just decided to throw this event away.
    return nsEventStatus_eConsumeNoDefault;
  }

  float spanRatio = event.mCurrentSpan / event.mPreviousSpan;

  {
    ReentrantMonitorAutoEnter monitor(mReentrantMonitor);

    float scale = mFrameMetrics.mResolution.width;

    nsIntPoint focusPoint = event.mFocusPoint;
    PRInt32 xFocusChange = (mLastZoomFocus.x - focusPoint.x) / scale, yFocusChange = (mLastZoomFocus.y - focusPoint.y) / scale;
    // If displacing by the change in focus point will take us off page bounds,
    // then reduce the displacement such that it doesn't.
    if (mX.DisplacementWillOverscroll(xFocusChange) != Axis::OVERSCROLL_NONE) {
      xFocusChange -= mX.DisplacementWillOverscrollAmount(xFocusChange);
    }
    if (mY.DisplacementWillOverscroll(yFocusChange) != Axis::OVERSCROLL_NONE) {
      yFocusChange -= mY.DisplacementWillOverscrollAmount(yFocusChange);
    }
    ScrollBy(nsIntPoint(xFocusChange, yFocusChange));

    // When we zoom in with focus, we can zoom too much towards the boundaries
    // that we actually go over them. These are the needed displacements along
    // either axis such that we don't overscroll the boundaries when zooming.
    PRInt32 neededDisplacementX = 0, neededDisplacementY = 0;

    // Only do the scaling if we won't go over 8x zoom in or out.
    bool doScale = (scale < 8.0f && spanRatio > 1.0f) || (scale > 0.125f && spanRatio < 1.0f);

    // If this zoom will take it over 8x zoom in either direction, but it's not
    // already there, then normalize it.
    if (scale * spanRatio > 8.0f) {
      spanRatio = scale / 8.0f;
    } else if (scale * spanRatio < 0.125f) {
      spanRatio = scale / 0.125f;
    }

    if (doScale) {
      switch (mX.ScaleWillOverscroll(spanRatio, focusPoint.x))
      {
        case Axis::OVERSCROLL_NONE:
          break;
        case Axis::OVERSCROLL_MINUS:
        case Axis::OVERSCROLL_PLUS:
          neededDisplacementX = -mX.ScaleWillOverscrollAmount(spanRatio, focusPoint.x);
          break;
        case Axis::OVERSCROLL_BOTH:
          // If scaling this way will make us overscroll in both directions, then
          // we must already be at the maximum zoomed out amount. In this case, we
          // don't want to allow this scaling to go through and instead clamp it
          // here.
          doScale = false;
          break;
      }
    }

    if (doScale) {
      switch (mY.ScaleWillOverscroll(spanRatio, focusPoint.y))
      {
        case Axis::OVERSCROLL_NONE:
          break;
        case Axis::OVERSCROLL_MINUS:
        case Axis::OVERSCROLL_PLUS:
          neededDisplacementY = -mY.ScaleWillOverscrollAmount(spanRatio, focusPoint.y);
          break;
        case Axis::OVERSCROLL_BOTH:
          doScale = false;
          break;
      }
    }

    if (doScale) {
      ScaleWithFocus(scale * spanRatio,
                     focusPoint);

      if (neededDisplacementX != 0 || neededDisplacementY != 0) {
        ScrollBy(nsIntPoint(neededDisplacementX, neededDisplacementY));
      }

      ForceRepaint();
      // We don't want to redraw on every scale, so don't use SendViewportChange()
    }

    mLastZoomFocus = focusPoint;
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnScaleEnd(const PinchEvent& event) {
  mState = PANNING;
  mX.StartTouch(event.mFocusPoint.x);
  mY.StartTouch(event.mFocusPoint.y);
  {
    ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
    ForceRepaint();
    SendViewportChange();
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnLongPress(const TapEvent& event) {
  // XXX: Should only send this if the zoom settings are actually valid.
  ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
  nsIntPoint actualPoint = ConvertViewPointToLayerPoint(event.mPoint);
  mGeckoContentController->SendGestureEvent(NS_LITERAL_STRING("Gesture:LongPress"), actualPoint);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnSingleTapUp(const TapEvent& event) {
  // XXX: Should only send this if the zoom settings are actually valid.
  ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
  nsIntPoint actualPoint = ConvertViewPointToLayerPoint(event.mPoint);
  mGeckoContentController->SendGestureEvent(NS_LITERAL_STRING("Gesture:SingleTap"), actualPoint);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnSingleTapConfirmed(const TapEvent& event) {
  // XXX: Should only send this if the zoom settings are actually valid.
  ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
  nsIntPoint actualPoint = ConvertViewPointToLayerPoint(event.mPoint);
  mGeckoContentController->SendGestureEvent(NS_LITERAL_STRING("Gesture:SingleTap"), actualPoint);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnDoubleTap(const TapEvent& event) {
  // XXX: Should only send this if the zoom settings are actually valid.
  ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
  nsIntPoint actualPoint = ConvertViewPointToLayerPoint(event.mPoint);
  mGeckoContentController->SendGestureEvent(NS_LITERAL_STRING("Gesture:DoubleTap"), actualPoint);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnCancelTap() {
  mGeckoContentController->SendGestureEvent(NS_LITERAL_STRING("Gesture:Cancel"), nsIntPoint(0, 0));

  return nsEventStatus_eConsumeNoDefault;
}

float AsyncPanZoomController::PanDistance(const MultiTouchEvent& event) {
  SingleTouchData& touch = GetTouchFromEvent(event);
  nsIntPoint point = touch.mScreenPoint;
  PRInt32 xPos = point.x, yPos = point.y;
  mX.UpdateWithTouchAtDevicePoint(xPos, 0);
  mY.UpdateWithTouchAtDevicePoint(yPos, 0);
  return sqrt(mX.PanDistance() * mX.PanDistance() + mY.PanDistance() * mY.PanDistance())
         * mFrameMetrics.mResolution.width;
}

const nsPoint AsyncPanZoomController::GetVelocityVector() {
  return nsPoint(
    mX.GetVelocity(),
    mY.GetVelocity()
  );
}

void AsyncPanZoomController::TrackTouch(const MultiTouchEvent& event) {
  SingleTouchData& touch = GetTouchFromEvent(event);
  nsIntPoint point = touch.mScreenPoint;
  PRInt32 xPos = point.x, yPos = point.y, timeDelta = event.mTime - mLastEventTime;

  // Probably a duplicate event, just throw it away.
  if (!timeDelta) {
    return;
  }

  {
    ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
    mX.UpdateWithTouchAtDevicePoint(xPos, timeDelta);
    mY.UpdateWithTouchAtDevicePoint(yPos, timeDelta);

    float scale = mFrameMetrics.mResolution.width;

    PRInt32 xDisplacement = mX.UpdateAndGetDisplacement(scale);
    PRInt32 yDisplacement = mY.UpdateAndGetDisplacement(scale);
    if (!xDisplacement && !yDisplacement) {
      return;
    }

    ScrollBy(nsIntPoint(xDisplacement, yDisplacement));
    ForceRepaint();

    if (event.mTime - mLastRepaint >= REPAINT_INTERVAL) {
      SendViewportChange();
      mLastRepaint = event.mTime;
    }
  }
}

SingleTouchData& AsyncPanZoomController::GetTouchFromEvent(const MultiTouchEvent& event) {
  return (SingleTouchData&)event.mTouches[0];
}

void AsyncPanZoomController::DoFling() {
  if (mState != FLING) {
    return;
  }

  {
    ReentrantMonitorAutoEnter monitor(mReentrantMonitor);
    if (!mX.FlingApplyFrictionOrCancel() && !mY.FlingApplyFrictionOrCancel()) {
      ForceRepaint();
      SendViewportChange();
      mState = NOTHING;
      return;
    }

    float scale = mFrameMetrics.mResolution.width;

    ScrollBy(nsIntPoint(
      mX.UpdateAndGetDisplacement(scale),
      mY.UpdateAndGetDisplacement(scale)
    ));
    ForceRepaint();
    SendViewportChange();
  }
}

void AsyncPanZoomController::CancelAnimation() {
  mState = NOTHING;
}

void AsyncPanZoomController::SetCompositorParent(CompositorParent* aCompositorParent) {
  mCompositorParent = aCompositorParent;
}

void AsyncPanZoomController::ScrollBy(const nsIntPoint& aOffset) {
  nsIntPoint newOffset(mFrameMetrics.mViewportScrollOffset.x + aOffset.x,
                       mFrameMetrics.mViewportScrollOffset.y + aOffset.y);
  FrameMetrics metrics(mFrameMetrics);
  metrics.mViewportScrollOffset = newOffset;
  SetFrameMetrics(metrics);
}

void AsyncPanZoomController::ScaleWithFocus(float aScale, const nsIntPoint& aFocus) {
  FrameMetrics metrics(mFrameMetrics);

  // Don't set the scale to the inputted value, but rather multiply it in.
  float scaleFactor = aScale / metrics.mResolution.width;

  // The page rect is the css page rect scaled by the current zoom.
  gfx::Rect cssPageSize = metrics.mCSSContentRect;
  cssPageSize.x *= aScale;
  cssPageSize.y *= aScale;
  cssPageSize.width *= aScale;
  cssPageSize.height *= aScale;
  metrics.mContentRect = nsIntRect(NS_lround(cssPageSize.x),
                                   NS_lround(cssPageSize.y),
                                   NS_lround(cssPageSize.width),
                                   NS_lround(cssPageSize.height));

  // To account for focus, offset the page by the focus point scaled.
  nsIntPoint scrollOffset = metrics.mViewportScrollOffset,
             originalOffset = scrollOffset;
  scrollOffset.x += aFocus.x + (scaleFactor < 1 ? 1 : -1) * originalOffset.x / aScale;
  scrollOffset.y += aFocus.y + (scaleFactor < 1 ? 1 : -1) * originalOffset.y / aScale;
  scrollOffset.x *= scaleFactor;
  scrollOffset.y *= scaleFactor;
  scrollOffset.x -= aFocus.x + (scaleFactor < 1 ? 1 : -1) * originalOffset.x / aScale;
  scrollOffset.y -= aFocus.y + (scaleFactor < 1 ? 1 : -1) * originalOffset.y / aScale;
  metrics.mViewportScrollOffset = scrollOffset;

  metrics.mResolution.width = metrics.mResolution.height = aScale;

  SetFrameMetrics(metrics);
}

const nsIntRect AsyncPanZoomController::CalculatePendingDisplayPort() {
  const float SIZE_MULTIPLIER = 2.0f;
  const float EPSILON = 0.00001;

  float scale = mFrameMetrics.mResolution.width;
  nsIntPoint scrollOffset = mFrameMetrics.mViewportScrollOffset;
  nsIntRect viewport = mFrameMetrics.mViewport;
  viewport.ScaleRoundIn(1 / scale);
  gfx::Rect contentRect = mFrameMetrics.mCSSContentRect;

  // Paint a larger portion of the screen than just what we can see. This makes
  // it less likely that we'll checkerboard when panning around and Gecko hasn't
  // repainted yet.
  float desiredWidth = viewport.width * SIZE_MULTIPLIER,
        desiredHeight = viewport.height * SIZE_MULTIPLIER;

  // The displayport is relative to the current scroll offset. Here's a little
  // diagram to make it easier to see:
  //
  //       - - - -
  //       |     |
  //    *************
  //    *  |     |  *
  // - -*- @------ -*- -
  // |  *  |=====|  *  |
  //    *  |=====|  *
  // |  *  |=====|  *  |
  // - -*- ------- -*- -
  //    *  |     |  *
  //    *************
  //       |     |
  //       - - - -
  //
  // The full --- area with === inside it is the actual viewport rect, the *** area
  // is the displayport, and the - - - area is an imaginary additional page on all 4
  // borders of the actual page. Notice that the displayport intersects half-way with
  // each of the imaginary extra pages. The @ symbol at the top left of the
  // viewport marks the current scroll offset. From the @ symbol to the far left
  // and far top, it is clear that this distance is 1/4 of the displayport's
  // height/width dimension.
  gfx::Rect displayPort(-desiredWidth / 4, -desiredHeight / 4, desiredWidth, desiredHeight);

  // Check if the desired boundaries go over the CSS page rect along the top or
  // left. If they do, shift them to the right or down.
  float oldDisplayPortX = displayPort.x, oldDisplayPortY = displayPort.y;
  if (displayPort.X() + scrollOffset.x < contentRect.X())
    displayPort.x = contentRect.X() - scrollOffset.x;
  if (displayPort.Y() + scrollOffset.y < contentRect.Y())
    displayPort.y = contentRect.Y() - scrollOffset.y;

  // We don't need to paint the extra area that was going to overlap with the
  // content rect. Subtract out this extra width or height.
  displayPort.width -= displayPort.x - oldDisplayPortX;
  displayPort.height -= displayPort.y - oldDisplayPortY;

  // Check if the desired boundaries go over the CSS page rect along the right
  // or bottom. If they do, subtract out some height or width such that they
  // perfectly align with the end of the CSS page rect.
  if (displayPort.XMost() + scrollOffset.x > contentRect.XMost())
    displayPort.width = NS_MAX(0.0f, contentRect.XMost() - (displayPort.X() + scrollOffset.x));
  if (displayPort.YMost() + scrollOffset.y > contentRect.YMost())
    displayPort.height = NS_MAX(0.0f, contentRect.YMost() - (displayPort.Y() + scrollOffset.y));

  return nsIntRect(NS_lround(displayPort.X()), NS_lround(displayPort.Y()), NS_lround(displayPort.Width()), NS_lround(displayPort.Height()));
}

void AsyncPanZoomController::SetDPI(int aDPI) {
  mDPI = aDPI;
  mPanThreshold = 1.0f/16.0f * mDPI;
}

const nsIntPoint AsyncPanZoomController::ConvertViewPointToLayerPoint(const nsIntPoint& viewPoint) {
  float scale = mFrameMetrics.mResolution.width;
  nsIntPoint offset = mFrameMetrics.mViewportScrollOffset;
  nsIntRect displayPort = mFrameMetrics.mDisplayPort;
  return nsIntPoint(offset.x + viewPoint.x / scale, offset.y + viewPoint.y / scale);
}

bool AsyncPanZoomController::GetLayersUpdated() {
  return mLayersUpdated;
}

void AsyncPanZoomController::ResetLayersUpdated() {
  mLayersUpdated = false;
}

void AsyncPanZoomController::ForceRepaint() {
  mLayersUpdated = true;
  if (mCompositorParent) {
    mCompositorParent->ScheduleRenderOnCompositorThread();
  }
}

void AsyncPanZoomController::SendViewportChange() {
  mFrameMetrics.mDisplayPort = CalculatePendingDisplayPort();
  mGeckoContentController->SendViewportChange(mFrameMetrics);
}

void AsyncPanZoomController::GetContentTransformForFrame(const FrameMetrics& aFrame,
                                                         const gfx3DMatrix& aRootTransform,
                                                         const gfxSize& aWidgetSize,
                                                         gfx3DMatrix* aTreeTransform,
                                                         gfxPoint* aReverseViewTranslation) {
  // Scales on the root layer, on what's currently painted.
  float rootScaleX = aRootTransform.GetXScale(),
        rootScaleY = aRootTransform.GetYScale();

  // Current local transform; this is not what's painted but rather what PZC has
  // transformed due to touches like panning or pinching. Eventually, the root
  // layer transform will become this during runtime, but we must wait for Gecko
  // to repaint.
  float localScaleX = mFrameMetrics.mResolution.width,
        localScaleY = mFrameMetrics.mResolution.height;

  // Handle transformations for asynchronous panning and zooming. We determine the
  // zoom used by Gecko from the transformation set on the root layer, and we
  // determine the scroll offset used by Gecko from the frame metrics of the
  // primary scrollable layer. We compare this to the desired zoom and scroll
  // offset in the view transform we obtained from Java in order to compute the
  // transformation we need to apply.
  float tempScaleDiffX = rootScaleX * localScaleX;
  float tempScaleDiffY = rootScaleY * localScaleY;

  nsIntPoint metricsScrollOffset(0, 0);
  if (aFrame.IsScrollable())
    metricsScrollOffset = aFrame.mViewportScrollOffset;

  nsIntPoint scrollCompensation(
    mFrameMetrics.mViewportScrollOffset.x / rootScaleX - metricsScrollOffset.x,
    mFrameMetrics.mViewportScrollOffset.y / rootScaleY - metricsScrollOffset.y);

  ViewTransform treeTransform(-scrollCompensation, localScaleX, localScaleY);
  *aTreeTransform = gfx3DMatrix(treeTransform);

  float offsetX = mFrameMetrics.mViewportScrollOffset.x / tempScaleDiffX,
        offsetY = mFrameMetrics.mViewportScrollOffset.y / tempScaleDiffY;

  nsIntRect localContentRect = mFrameMetrics.mContentRect;
  offsetX = NS_MAX((float)localContentRect.x,
                   NS_MIN(offsetX, (float)(localContentRect.XMost() - aWidgetSize.width)));
  offsetY = NS_MAX((float)localContentRect.y,
                    NS_MIN(offsetY, (float)(localContentRect.YMost() - aWidgetSize.height)));
  *aReverseViewTranslation = gfxPoint(offsetX - metricsScrollOffset.x,
                                      offsetY - metricsScrollOffset.y);
}

void AsyncPanZoomController::NotifyLayersUpdated(const FrameMetrics& aViewportFrame) {

}

const FrameMetrics& AsyncPanZoomController::GetFrameMetrics() {
  return mFrameMetrics;
}

void AsyncPanZoomController::SetFrameMetrics(const FrameMetrics& aFrameMetrics) {
  mFrameMetrics = aFrameMetrics;
}

ReentrantMonitor& AsyncPanZoomController::GetReentrantMonitor() {
  return mReentrantMonitor;
}

void AsyncPanZoomController::SetCompositing(bool aCompositing) {
  mIsCompositing = aCompositing;
}

void AsyncPanZoomController::UpdateViewportSize(int width, int height) {
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  FrameMetrics metrics = GetFrameMetrics();
  metrics.mViewport = nsIntRect(0, 0, width, height);
  SetFrameMetrics(metrics);
}

}
}
