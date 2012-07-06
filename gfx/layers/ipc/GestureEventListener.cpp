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
    HandlePinchEvent(event, true);

  case NS_TOUCH_START_POINTER: {
    for (size_t i = 0; i < event.touchData.Length(); i++) {
      bool foundAlreadyExistingTouch = false;
      for (size_t j = 0; j < mTouches.Length(); j++) {
        if (mTouches[j].GetIdentifier() == event.touchData[i].GetIdentifier()) {
          foundAlreadyExistingTouch = true;
        }
      }

      NS_WARN_IF_FALSE(!foundAlreadyExistingTouch, "Tried to add a touch that already exists");

      // If we didn't find a touch in our list that matches this, then add it.
      // If it already existed, we don't want to add it twice because that
      // messes with our touch move/end code.
      if (!foundAlreadyExistingTouch)
        mTouches.AppendElement(event.touchData[i]);

      char thing[256];
      sprintf(thing, "ADDED ID: %d", event.touchData[i].GetIdentifier());
      NS_ASSERTION(false, thing);
    }

    if (mTouches.Length() == 2) {
      // Another finger has been added; it can't be a tap anymore.
      HandleTapCancel(event);
    }

    break;
  }
  case NS_TOUCH_MOVE: {
    // If we move at all, just bail out of the tap.
    HandleTapCancel(event);

    char thing[256];
    sprintf(thing, "!!!!!!!!!!!!!!!!!!TOUCH MOVED: %d [%d total]", event.touchData[0].GetIdentifier(), event.touchData.Length());
    NS_ASSERTION(false, thing);
    bool foundAlreadyExistingTouch = false;
    for (size_t i = 0; i < mTouches.Length(); i++) {
      for (size_t j = 0; j < event.touchData.Length(); j++) {
        if (mTouches[i].GetIdentifier() == event.touchData[j].GetIdentifier()) {
          foundAlreadyExistingTouch = true;
          mTouches[i] = event.touchData[j];
        }
      }
    }

    NS_WARN_IF_FALSE(foundAlreadyExistingTouch, "Touch moved, but not in list");

    break;
  }
  case NS_TOUCH_END: {
    bool foundAlreadyExistingTouch = false;
    for (size_t i = 0; i < event.touchData.Length() && !foundAlreadyExistingTouch; i++) {
      for (size_t j = 0; j < mTouches.Length() && !foundAlreadyExistingTouch; j++) {
        if (event.touchData[i].GetIdentifier() == mTouches[j].GetIdentifier()) {
          foundAlreadyExistingTouch = true;
          char thing[256];
          sprintf(thing, "REMOVED ID: %d / %d", event.touchData[i].GetIdentifier(), mTouches.Length());
          NS_ASSERTION(false, thing);
          mTouches.RemoveElementAt(j);
          sprintf(thing, "AFTER: %d", mTouches.Length());
          NS_ASSERTION(false, thing);
        }
      }
    }

    NS_WARN_IF_FALSE(foundAlreadyExistingTouch, "Touch ended, but not in list");

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
  }
  case NS_TOUCH_CANCEL:
    HandlePinchEvent(event, true);
    NS_ASSERTION(false, "CANCEL!!!!");
    break;

  }

  if (HandlePinchEvent(event, false) == nsEventStatus_eConsumeNoDefault)
    return nsEventStatus_eConsumeNoDefault;

  return mAsyncPanZoomController->HandleInputEvent(event);
}

nsEventStatus GestureEventListener::HandlePinchEvent(const nsTouchEvent& event, bool clearTouches)
{
  size_t uniqueTouches = 0;
  for (size_t i = 0; i < mTouches.Length(); i++) {
    if (mTouches[i].GetIdentifier() != mTouches[0].GetIdentifier()) {
      uniqueTouches++;
    }
  }

  if (/*uniqueTouches > 1*/ mTouches.Length() > 1 && !clearTouches) {
    NS_ASSERTION(false, "((((((((((((((((((((((((((MULTIPLE TOUCHES");
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
      NS_ASSERTION(false, "((((((((((((((((((((((((SENDING SCALE");
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
    pinchEvent.focusPoint = mTouches[0].GetPoint();
    for (size_t i = 0; i < mTouches.Length(); i++) {
      char thing[256];
      sprintf(thing, "ID LEFT[%d]: %d", i, mTouches[i].GetIdentifier());
      NS_ASSERTION(false, thing);
      if (mTouches[i].GetIdentifier() != event.touchData[0].GetIdentifier()) {
        pinchEvent.focusPoint = mTouches[i].GetPoint();
        break;
      }
    }

    //if (uniqueTouches < mTouches.Length()) {
    //  mTouches.RemoveElementsAt(uniqueTouches, mTouches.Length() - 1);
    //}

    if (clearTouches) {
      mTouches.Clear();
    }

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
