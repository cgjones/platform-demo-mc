/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoContentControllerAndroid.h"

#include "AndroidJavaWrappers.h"
#include "nsAppShell.h"

namespace mozilla {
namespace layers {

void GeckoContentControllerAndroid::SendGestureEvent(const nsAString& aTopic,
                                                     const nsIntPoint& aPoint)
{
  GeckoContentController::SendGestureEvent(aTopic, aPoint);

  if (aTopic.Equals(NS_LITERAL_STRING("Gesture:SingleTap"))) {
    AndroidGeckoEvent* event = new AndroidGeckoEvent();
    event->mType = AndroidGeckoEvent::MOTION_EVENT;
    event->mAction = AndroidKeyEvent::ACTION_DOWN;
    event->mPoints.AppendElement(aPoint);
    event->mPointerIndex = 0;
    event->mCount = 1;

    // Extra junk that has to be added for the Android widget code to be able to
    // use this. Normally we care about what data it contains, but not in this
    // case.
    event->mPointIndicies.AppendElement(0);
    event->mPointRadii.AppendElement(nsIntPoint(1, 1));
    event->mOrientations.AppendElement(90.0f);
    event->mPressures.AppendElement(1.0f);

    nsAppShell::gAppShell->PostEvent(event);
  } else if (aTopic.Equals(NS_LITERAL_STRING("Gesture:Cancel"))) {
    AndroidGeckoEvent* event = new AndroidGeckoEvent();
    event->mType = AndroidGeckoEvent::MOTION_EVENT;
    event->mAction = AndroidKeyEvent::ACTION_UP;
    event->mPoints.AppendElement(aPoint);
    event->mPointerIndex = 0;
    event->mCount = 1;

    event->mPointIndicies.AppendElement(0);
    event->mPointRadii.AppendElement(nsIntPoint(1, 1));
    event->mOrientations.AppendElement(90.0f);
    event->mPressures.AppendElement(1.0f);

    nsAppShell::gAppShell->PostEvent(event);
  }
}

}
}
