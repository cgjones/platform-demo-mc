/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef InputEvent_h__
#define InputEvent_h__

#include "nsGUIEvent.h"
#include "nsDOMTouchEvent.h"

#define MULTITOUCH_EVENT_START    100
#define MULTITOUCH_START_POINTER  (MULTITOUCH_EVENT_START)
#define MULTITOUCH_START          (MULTITOUCH_EVENT_START+1)
#define MULTITOUCH_MOVE           (MULTITOUCH_EVENT_START+2)
#define MULTITOUCH_END            (MULTITOUCH_EVENT_START+3)
#define MULTITOUCH_ENTER          (MULTITOUCH_EVENT_START+4)
#define MULTITOUCH_LEAVE          (MULTITOUCH_EVENT_START+5)
#define MULTITOUCH_CANCEL         (MULTITOUCH_EVENT_START+6)

#define PINCH_EVENT_START         200
#define PINCH_START               (PINCH_EVENT_START)
#define PINCH_SCALE               (PINCH_EVENT_START+1)
#define PINCH_END                 (PINCH_EVENT_START+2)

#define TAP_EVENT_START           300
#define TAP_LONG                  (TAP_EVENT_START)
#define TAP_UP                    (TAP_EVENT_START+1)
#define TAP_CONFIRMED             (TAP_EVENT_START+2)
#define TAP_DOUBLE                (TAP_EVENT_START+3)
#define TAP_CANCEL                (TAP_EVENT_START+4)

namespace mozilla {

/** Base input event class. Should never be instantiated. */
class InputEvent {
public:
  PRUint32 mMessage;
  PRUint32 mTime;

protected:
  InputEvent(PRUint32 aMessage, PRUint32 aTime)
    : mMessage(aMessage),
      mTime(aTime)
  {

  }
};

class SingleTouchData {
public:
  SingleTouchData(PRInt32 aIdentifier,
                  nsIntPoint aScreenPoint,
                  nsIntPoint aRadius,
                  float aRotationAngle,
                  float aForce)
    : mIdentifier(aIdentifier),
      mScreenPoint(aScreenPoint),
      mRadius(aRadius),
      mRotationAngle(aRotationAngle),
      mForce(aForce)
  {

  }

  PRInt32 mIdentifier;
  nsIntPoint mScreenPoint;
  nsIntPoint mRadius;
  float mRotationAngle;
  float mForce;
};

class MultiTouchEvent : public InputEvent {
public:
  MultiTouchEvent(PRUint32 aMessage, PRUint32 aTime)
    : InputEvent(aMessage, aTime)
  {

  }

  MultiTouchEvent(const nsTouchEvent& aTouchEvent)
    : InputEvent(0, aTouchEvent.time)
  {
    NS_ABORT_IF_FALSE(NS_IsMainThread(),
                      "Can only copy from nsTouchEvent on main thread");

    switch (aTouchEvent.message) {
      case NS_TOUCH_START:
        mMessage = MULTITOUCH_START;
        break;
      case NS_TOUCH_MOVE:
        mMessage = MULTITOUCH_MOVE;
        break;
      case NS_TOUCH_END:
        mMessage = MULTITOUCH_END;
        break;
      case NS_TOUCH_ENTER:
        mMessage = MULTITOUCH_ENTER;
        break;
      case NS_TOUCH_LEAVE:
        mMessage = MULTITOUCH_LEAVE;
        break;
      case NS_TOUCH_CANCEL:
        mMessage = MULTITOUCH_CANCEL;
        break;
      default:
        break;
    }

    for (size_t i = 0; i < aTouchEvent.touches.Length(); i++) {
      nsDOMTouch& domTouch = (nsDOMTouch&)(*aTouchEvent.touches[i].get());

      // Extract data from weird interfaces.
      PRInt32 identifier, radiusX, radiusY;
      float rotationAngle, force;
      domTouch.GetIdentifier(&identifier);
      domTouch.GetRadiusX(&radiusX);
      domTouch.GetRadiusY(&radiusY);
      domTouch.GetRotationAngle(&rotationAngle);
      domTouch.GetForce(&force);

      SingleTouchData data(identifier,
                           domTouch.mRefPoint,
                           nsIntPoint(radiusX, radiusY),
                           rotationAngle,
                           force);

      mTouches.AppendElement(data);
    }
  }

  nsTArray<SingleTouchData> mTouches;
};

class PinchEvent : public InputEvent {
public:
  PinchEvent(PRUint32 aMessage,
             PRUint32 aTime,
             const nsIntPoint& aFocusPoint,
             float aCurrentSpan,
             float aPreviousSpan)
    : InputEvent(aMessage, aTime),
      mFocusPoint(aFocusPoint),
      mCurrentSpan(aCurrentSpan),
      mPreviousSpan(aPreviousSpan)
  {

  }

  nsIntPoint mFocusPoint;
  float mCurrentSpan;
  float mPreviousSpan;
};

class TapEvent : public InputEvent {
public:
  TapEvent(PRUint32 aMessage, PRUint32 aTime, const nsIntPoint& aPoint)
    : InputEvent(aMessage, aTime),
      mPoint(aPoint)
  {

  }

  nsIntPoint mPoint;
};

}

#endif // InputEvent_h__
