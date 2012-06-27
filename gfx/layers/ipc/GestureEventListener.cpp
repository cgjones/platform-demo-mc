/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GestureEventListener.h"
#include "AsyncPanZoomController.h"

namespace mozilla {
namespace layers {

const long GestureEventListener::MAX_TAP_TIME = 500;

GestureEventListener::GestureEventListener(AsyncPanZoomController* aAsyncPanZoomController)
  : mAsyncPanZoomController(aAsyncPanZoomController),
    mState(NoGesture)
{

}

GestureEventListener::~GestureEventListener()
{

}

nsEventStatus GestureEventListener::HandleTouchEvent(const nsTouchEvent& event)
{
  switch (event.message)
  {
  case NS_TOUCH_START:
    mTouchStartTime = event.time;
    mTouches.Clear();

  case NS_TOUCH_START_POINTER:
    for (size_t i = 0; i < event.touchData.Length(); i++) {
      mTouches.AppendElement(event.touchData[i]);
    }

    if (mTouches.Length() == 2) {
      // Another finger has been added; it can't be a tap anymore.
      HandleTapCancel(event);
    }

    break;

  case NS_TOUCH_MOVE:
    // If we move at all, just bail out of the tap.
    HandleTapCancel(event);

    for (size_t i = 0; i < mTouches.Length(); i++) {
      for (size_t j = 0; j < event.touchData.Length(); j++) {
        if (mTouches[i].GetIdentifier() == event.touchData[j].GetIdentifier()) {
          mTouches[i] = event.touchData[j];
          break;
        }
      }
    }

    break;
  case NS_TOUCH_END:
    for (size_t i = 0; i < mTouches.Length(); i++) {
      for (size_t j = 0; j < event.touchData.Length(); j++) {
        if (mTouches[i].GetIdentifier() == event.touchData[j].GetIdentifier()) {
          mTouches.RemoveElementAt(i);
        }
      }
    }

    if (event.time - mTouchStartTime <= MAX_TAP_TIME) {
      // XXX: Incorrect use of the tap event. In the future, we want to send this
      // on NS_TOUCH_END, then have a short timer afterwards which sends
      // SingleTapConfirmed. Since we don't have double taps yet, this is fine for
      // now.
      if (HandleSingleTapUpEvent(event) == nsEventStatus_eConsumeNoDefault) {
        return nsEventStatus_eConsumeNoDefault;
      }

      if (HandleSingleTapConfirmedEvent(event) == nsEventStatus_eConsumeNoDefault) {
        return nsEventStatus_eConsumeNoDefault;
      }
    }

    break;

  case NS_TOUCH_CANCEL:
    mTouches.Clear();
    break;

  }

  if (HandlePinchEvent(event) == nsEventStatus_eConsumeNoDefault)
    return nsEventStatus_eConsumeNoDefault;

  return mAsyncPanZoomController->HandleInputEvent(event);
}

nsEventStatus GestureEventListener::HandlePinchEvent(const nsTouchEvent& event)
{
  if (mTouches.Length() > 1) {
    SingleTouchData &firstTouch = mTouches[0],
                    &secondTouch = mTouches[mTouches.Length() - 1];
    nsIntPoint focusPoint =
      nsIntPoint((firstTouch.GetPoint().x + secondTouch.GetPoint().x)/2,
                 (firstTouch.GetPoint().y + secondTouch.GetPoint().y)/2);
    float currentSpan =
      sqrt(float((firstTouch.GetPoint().x - secondTouch.GetPoint().x) *
                 (firstTouch.GetPoint().x - secondTouch.GetPoint().x) +
                 (firstTouch.GetPoint().y - secondTouch.GetPoint().y) *
                 (firstTouch.GetPoint().y - secondTouch.GetPoint().y)));

    if (mState == NoGesture) {
      nsPinchEvent pinchEvent(true, NS_PINCH_START, nsnull);
      pinchEvent.time = event.time;
      pinchEvent.focusPoint = focusPoint;
      pinchEvent.currentSpan = currentSpan;
      pinchEvent.previousSpan = currentSpan;

      mAsyncPanZoomController->HandleInputEvent(pinchEvent);

      mState = InPinchGesture;
    } else {
      nsPinchEvent pinchEvent(true, NS_PINCH_SCALE, nsnull);
      pinchEvent.time = event.time;
      pinchEvent.focusPoint = focusPoint;
      pinchEvent.currentSpan = currentSpan;
      pinchEvent.previousSpan = mPreviousSpan;

      mAsyncPanZoomController->HandleInputEvent(pinchEvent);
    }
    mPreviousSpan = currentSpan;
    return nsEventStatus_eConsumeNoDefault;
  } else if (mState == InPinchGesture) {
    nsPinchEvent pinchEvent(true, NS_PINCH_END, nsnull);
    pinchEvent.time = event.time;
    pinchEvent.focusPoint = event.touchData[0].GetPoint();

    mAsyncPanZoomController->HandleInputEvent(pinchEvent);

    mState = NoGesture;

    return nsEventStatus_eConsumeNoDefault;
  }
  return nsEventStatus_eIgnore;
}

nsEventStatus GestureEventListener::HandleSingleTapUpEvent(const nsTouchEvent& event)
{
  nsTapEvent tapEvent(true, NS_TAP_UP, nsnull);
  tapEvent.point = event.touchData[0].GetPoint();
  tapEvent.time = event.time;

  mAsyncPanZoomController->HandleInputEvent(tapEvent);

  return nsEventStatus_eConsumeDoDefault;
}

nsEventStatus GestureEventListener::HandleSingleTapConfirmedEvent(const nsTouchEvent& event)
{
  nsTapEvent tapEvent(true, NS_TAP_CONFIRMED, nsnull);
  tapEvent.point = event.touchData[0].GetPoint();
  tapEvent.time = event.time;

  mAsyncPanZoomController->HandleInputEvent(tapEvent);

  return nsEventStatus_eConsumeDoDefault;
}

nsEventStatus GestureEventListener::HandleTapCancel(const nsTouchEvent& event)
{
  // XXX: In the future we will have to actually send a cancel notification to
  // Gecko, but for now since we're doing both the "SingleUp" and
  // "SingleConfirmed" notifications together, there's no need to cancel either
  // one.
  mTouchStartTime = 0;
  return nsEventStatus_eConsumeDoDefault;
}

AsyncPanZoomController* GestureEventListener::GetAsyncPanZoomController() {
  return mAsyncPanZoomController;
}

}
}
