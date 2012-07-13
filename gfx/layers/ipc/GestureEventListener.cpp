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

nsEventStatus GestureEventListener::HandleTouchEvent(const MultiTouchEvent& event)
{
  switch (event.mMessage)
  {
  case MULTITOUCH_START:
    mTouchStartTime = event.mTime;
    HandlePinchEvent(event, true);

  case MULTITOUCH_START_POINTER: {
    for (size_t i = 0; i < event.mTouches.Length(); i++) {
      bool foundAlreadyExistingTouch = false;
      for (size_t j = 0; j < mTouches.Length(); j++) {
        if (mTouches[j].mIdentifier == event.mTouches[i].mIdentifier) {
          foundAlreadyExistingTouch = true;
        }
      }

      NS_WARN_IF_FALSE(!foundAlreadyExistingTouch, "Tried to add a touch that already exists");

      // If we didn't find a touch in our list that matches this, then add it.
      // If it already existed, we don't want to add it twice because that
      // messes with our touch move/end code.
      if (!foundAlreadyExistingTouch)
        mTouches.AppendElement(event.mTouches[i]);
    }

    if (mTouches.Length() == 2) {
      // Another finger has been added; it can't be a tap anymore.
      HandleTapCancel(event);
    }

    break;
  }
  case MULTITOUCH_MOVE: {
    // If we move at all, just bail out of the tap.
    HandleTapCancel(event);

    bool foundAlreadyExistingTouch = false;
    for (size_t i = 0; i < mTouches.Length(); i++) {
      for (size_t j = 0; j < event.mTouches.Length(); j++) {
        if (mTouches[i].mIdentifier == event.mTouches[j].mIdentifier) {
          foundAlreadyExistingTouch = true;
          mTouches[i] = event.mTouches[j];
        }
      }
    }

    NS_WARN_IF_FALSE(foundAlreadyExistingTouch, "Touch moved, but not in list");

    break;
  }
  case MULTITOUCH_END: {
    bool foundAlreadyExistingTouch = false;
    for (size_t i = 0; i < event.mTouches.Length() && !foundAlreadyExistingTouch; i++) {
      for (size_t j = 0; j < mTouches.Length() && !foundAlreadyExistingTouch; j++) {
        if (event.mTouches[i].mIdentifier == mTouches[j].mIdentifier) {
          foundAlreadyExistingTouch = true;
          mTouches.RemoveElementAt(j);
        }
      }
    }

    NS_WARN_IF_FALSE(foundAlreadyExistingTouch, "Touch ended, but not in list");

    if (event.mTime - mTouchStartTime <= MAX_TAP_TIME) {
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
  }
  case MULTITOUCH_CANCEL:
    HandlePinchEvent(event, true);
    break;

  }

  if (HandlePinchEvent(event, false) == nsEventStatus_eConsumeNoDefault)
    return nsEventStatus_eConsumeNoDefault;

  return mAsyncPanZoomController->HandleInputEvent(event);
}

nsEventStatus GestureEventListener::HandlePinchEvent(const MultiTouchEvent& event, bool clearTouches)
{
  if (mTouches.Length() > 1 && !clearTouches) {
    const nsIntPoint& firstTouch = mTouches[0].mScreenPoint,
                      secondTouch = mTouches[mTouches.Length() - 1].mScreenPoint;
    nsIntPoint focusPoint =
      nsIntPoint((firstTouch.x + secondTouch.x)/2,
                 (firstTouch.y + secondTouch.y)/2);
    float currentSpan =
      sqrt(float((firstTouch.x - secondTouch.x) *
                 (firstTouch.x - secondTouch.x) +
                 (firstTouch.y - secondTouch.y) *
                 (firstTouch.y - secondTouch.y)));

    if (mState == NoGesture) {
      PinchEvent pinchEvent(PINCH_START,
                            event.mTime,
                            focusPoint,
                            currentSpan,
                            currentSpan);

      mAsyncPanZoomController->HandleInputEvent(pinchEvent);

      mState = InPinchGesture;
    } else {
      PinchEvent pinchEvent(PINCH_SCALE,
                            event.mTime,
                            focusPoint,
                            currentSpan,
                            mPreviousSpan);

      mAsyncPanZoomController->HandleInputEvent(pinchEvent);
    }
    mPreviousSpan = currentSpan;
    return nsEventStatus_eConsumeNoDefault;
  } else if (mState == InPinchGesture) {
    PinchEvent pinchEvent(PINCH_END,
                          event.mTime,
                          mTouches[0].mScreenPoint,
                          1.0f,
                          1.0f);

    if (clearTouches) {
      mTouches.Clear();
    }

    mAsyncPanZoomController->HandleInputEvent(pinchEvent);

    mState = NoGesture;

    return nsEventStatus_eConsumeNoDefault;
  }
  return nsEventStatus_eIgnore;
}

nsEventStatus GestureEventListener::HandleSingleTapUpEvent(const MultiTouchEvent& event)
{
  TapEvent tapEvent(TAP_UP, event.mTime, event.mTouches[0].mScreenPoint);
  mAsyncPanZoomController->HandleInputEvent(tapEvent);

  return nsEventStatus_eConsumeDoDefault;
}

nsEventStatus GestureEventListener::HandleSingleTapConfirmedEvent(const MultiTouchEvent& event)
{
  TapEvent tapEvent(TAP_CONFIRMED, event.mTime, event.mTouches[0].mScreenPoint);
  mAsyncPanZoomController->HandleInputEvent(tapEvent);

  return nsEventStatus_eConsumeDoDefault;
}

nsEventStatus GestureEventListener::HandleTapCancel(const MultiTouchEvent& event)
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
