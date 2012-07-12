/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Axis.h"
#include "AsyncPanZoomController.h"

namespace mozilla {
namespace layers {

const float Axis::EPSILON = 0.0001;
const float Axis::MS_PER_FRAME = 1000.0f / 60.0f;
const float Axis::MAX_EVENT_ACCELERATION = 12;
const float Axis::FLING_FRICTION_FAST = 0.970;
const float Axis::FLING_FRICTION_SLOW = 0.850;
const float Axis::VELOCITY_THRESHOLD = 10;
const float Axis::FLING_STOPPED_THRESHOLD = 0.1f;
const float Axis::SNAP_LIMIT = 300.0f;

Axis::Axis(AsyncPanZoomController* aAsyncPanZoomController)
  : mPos(0.0f),
    mVelocity(0.0f),
    mAsyncPanZoomController(aAsyncPanZoomController)
{

}

void Axis::UpdateWithTouchAtDevicePoint(PRInt32 pos, PRInt32 timeDelta) {
  float newVelocity = MS_PER_FRAME * (mPos - pos) / timeDelta;

  bool curVelocityIsLow = fabsf(newVelocity) < 1.0f;
  bool directionChange = (mVelocity > 0) != (newVelocity != 0);

  // If a direction change has happened, or the current velocity due to this new
  // touch is relatively low, then just apply it. If not, throttle it.
  if (curVelocityIsLow || (directionChange && fabs(newVelocity) - EPSILON <= 0.0f)) {
    mVelocity = newVelocity;
  } else {
    float maxChange = fabsf(mVelocity * timeDelta * MAX_EVENT_ACCELERATION);
    mVelocity = NS_MIN(mVelocity + maxChange, NS_MAX(mVelocity - maxChange, newVelocity));
  }

  mVelocity = newVelocity;
  mPos = pos;
}

void Axis::StartTouch(PRInt32 pos) {
  mStartPos = pos;
  mPos = pos;
  mVelocity = 0.0f;
}

PRInt32 Axis::UpdateAndGetDisplacement(float scale) {
  PRInt32 displacement = NS_lround(mVelocity / scale);
  // If this displacement will cause an overscroll, throttle it. Can potentially
  // bring it to 0 even if the velocity is high.
  if (DisplacementWillOverscroll(displacement) != OVERSCROLL_NONE) {
    displacement -= DisplacementWillOverscrollAmount(displacement);
  }
  return displacement;
}

float Axis::PanDistance() {
  return fabsf(mPos - mStartPos);
}

void Axis::StopTouch() {
  mVelocity = 0.0f;
}

bool Axis::FlingApplyFrictionOrCancel() {
  if (fabsf(mVelocity) <= FLING_STOPPED_THRESHOLD) {
    // If the velocity is very low, just set it to 0-and stop the fling,
    // otherwise we'll just asymptotically approach 0 and the user won't
    // actually see any changes.
    mVelocity = 0.0f;
    return false;
  } else if (fabsf(mVelocity) >= VELOCITY_THRESHOLD) {
    mVelocity *= FLING_FRICTION_FAST;
  } else {
    mVelocity *= FLING_FRICTION_SLOW;
  }
  return true;
}

Axis::Overscroll Axis::GetOverscroll() {
  // If the current pan takes the viewport to the left of or above the current
  // page rect.
  bool minus = GetOrigin() < GetPageStart();
  // If the current pan takes the viewport to the right of or below the current
  // page rect.
  bool plus = GetViewportEnd() > GetPageEnd();
  if (minus && plus) {
    return OVERSCROLL_BOTH;
  } else if (minus) {
    return OVERSCROLL_MINUS;
  } else if (plus) {
    return OVERSCROLL_PLUS;
  }
  return OVERSCROLL_NONE;
}

PRInt32 Axis::GetExcess() {
  switch (GetOverscroll()) {
  case OVERSCROLL_MINUS: return GetOrigin() - GetPageStart();
  case OVERSCROLL_PLUS: return GetViewportEnd() - GetPageEnd();
  case OVERSCROLL_BOTH: return (GetViewportEnd() - GetPageEnd()) + (GetPageStart() - GetOrigin());
  default: return 0;
  }
}

Axis::Overscroll Axis::DisplacementWillOverscroll(PRInt32 displacement) {
  // If the current pan plus a displacement takes the viewport to the left of or
  // above the current page rect.
  bool minus = GetOrigin() + displacement < GetPageStart();
  // If the current pan plus a displacement takes the viewport to the right of or
  // below the current page rect.
  bool plus = GetViewportEnd() + displacement > GetPageEnd();
  if (minus && plus) {
    return OVERSCROLL_BOTH;
  } else if (minus) {
    return OVERSCROLL_MINUS;
  } else if (plus) {
    return OVERSCROLL_PLUS;
  }
  return OVERSCROLL_NONE;
}

PRInt32 Axis::DisplacementWillOverscrollAmount(PRInt32 displacement) {
  switch (DisplacementWillOverscroll(displacement)) {
  case OVERSCROLL_MINUS: return (GetOrigin() + displacement) - GetPageStart();
  case OVERSCROLL_PLUS: return (GetViewportEnd() + displacement) - GetPageEnd();
  // Don't handle overscrolled in both directions; a displacement can't cause
  // this, it must have already been zoomed out too far.
  default: return 0;
  }
}

Axis::Overscroll Axis::ScaleWillOverscroll(float scale, PRInt32 focus) {
  PRInt32 originAfterScale = NS_lround((GetOrigin() + focus) * scale - focus);

  bool both = ScaleWillOverscrollBothWays(scale);
  bool minus = originAfterScale < NS_lround(GetPageStart() * scale);
  bool plus = (originAfterScale + GetViewportLength()) > NS_lround(GetPageEnd() * scale);

  if ((minus && plus) || both) {
    return OVERSCROLL_BOTH;
  } else if (minus) {
    return OVERSCROLL_MINUS;
  } else if (plus) {
    return OVERSCROLL_PLUS;
  }
  return OVERSCROLL_NONE;
}

PRInt32 Axis::ScaleWillOverscrollAmount(float scale, PRInt32 focus) {
  PRInt32 originAfterScale = NS_lround((GetOrigin() + focus) * scale - focus);
  switch (ScaleWillOverscroll(scale, focus)) {
  case OVERSCROLL_MINUS: return originAfterScale - NS_lround(GetPageStart() * scale);
  case OVERSCROLL_PLUS: return (originAfterScale + GetViewportLength()) - NS_lround(GetPageEnd() * scale);
  // Don't handle OVERSCROLL_BOTH. Client code is expected to deal with it.
  default: return 0;
  }
}

float Axis::GetVelocity() {
  return mVelocity;
}

PRInt32 Axis::GetViewportEnd() {
  return GetOrigin() + GetViewportLength();
}

PRInt32 Axis::GetPageEnd() {
  return GetPageStart() + GetPageLength();
}

AxisX::AxisX(AsyncPanZoomController* aAsyncPanZoomController)
  : Axis(aAsyncPanZoomController)
{

}

PRInt32 AxisX::GetOrigin() {
  nsIntPoint origin = mAsyncPanZoomController->GetFrameMetrics().mViewportScrollOffset;
  return origin.x;
}

PRInt32 AxisX::GetViewportLength() {
  nsIntRect viewport = mAsyncPanZoomController->GetFrameMetrics().mViewport;
  return viewport.width;
}

PRInt32 AxisX::GetPageStart() {
  nsIntRect pageRect = mAsyncPanZoomController->GetFrameMetrics().mContentRect;
  return pageRect.x;
}

PRInt32 AxisX::GetPageLength() {
  nsIntRect pageRect = mAsyncPanZoomController->GetFrameMetrics().mContentRect;
  return pageRect.width;
}

bool AxisX::ScaleWillOverscrollBothWays(float scale) {
  const FrameMetrics& metrics = mAsyncPanZoomController->GetFrameMetrics();

  float currentScale = metrics.mResolution.width;
  gfx::Rect cssContentRect = metrics.mCSSContentRect;
  nsIntRect contentRect = nsIntRect(NS_lround(cssContentRect.x),
                                    NS_lround(cssContentRect.y),
                                    NS_lround(cssContentRect.width),
                                    NS_lround(cssContentRect.height)),
            viewport = metrics.mViewport;

  contentRect.ScaleRoundOut(scale * currentScale);

  return contentRect.width < viewport.width;
}

AxisY::AxisY(AsyncPanZoomController* aAsyncPanZoomController)
  : Axis(aAsyncPanZoomController)
{

}

PRInt32 AxisY::GetOrigin() {
  nsIntPoint origin = mAsyncPanZoomController->GetFrameMetrics().mViewportScrollOffset;
  return origin.y;
}

PRInt32 AxisY::GetViewportLength() {
  nsIntRect viewport = mAsyncPanZoomController->GetFrameMetrics().mViewport;
  return viewport.height;
}

PRInt32 AxisY::GetPageStart() {
  nsIntRect pageRect = mAsyncPanZoomController->GetFrameMetrics().mContentRect;
  return pageRect.y;
}

PRInt32 AxisY::GetPageLength() {
  nsIntRect pageRect = mAsyncPanZoomController->GetFrameMetrics().mContentRect;
  return pageRect.height;
}

bool AxisY::ScaleWillOverscrollBothWays(float scale) {
  const FrameMetrics& metrics = mAsyncPanZoomController->GetFrameMetrics();

  float currentScale = metrics.mResolution.width;
  gfx::Rect cssContentRect = metrics.mCSSContentRect;
  nsIntRect contentRect = nsIntRect(NS_lround(cssContentRect.x),
                                    NS_lround(cssContentRect.y),
                                    NS_lround(cssContentRect.width),
                                    NS_lround(cssContentRect.height)),
            viewport = metrics.mViewport;

  contentRect.ScaleRoundOut(scale * currentScale);

  return contentRect.height < viewport.height;
}

}
}
