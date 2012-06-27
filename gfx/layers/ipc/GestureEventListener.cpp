/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GestureEventListener.h"
#include "AsyncPanZoomController.h"

namespace mozilla {
namespace layers {

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
    mTouches.Clear();
  case NS_TOUCH_START_POINTER:
    for (size_t i = 0; i < event.touchData.Length(); i++) {
      mTouches.AppendElement(event.touchData[i]);
    }
    break;
  case NS_TOUCH_MOVE:
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
    break;
  case NS_TOUCH_CANCEL:
    mTouches.Clear();
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

AsyncPanZoomController* GestureEventListener::GetAsyncPanZoomController() {
  return mAsyncPanZoomController;
}

}
}
