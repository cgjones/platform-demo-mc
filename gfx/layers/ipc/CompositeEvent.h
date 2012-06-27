/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_CompositeEvent_h
#define mozilla_layers_CompositeEvent_h

#include "nsIThreadManager.h"
#include "nsThreadUtils.h"
#include "nsIObserverService.h"
#include "nsXPCOMStrings.h"
#include "nsString.h"

/**
 * Various compositing events. Used to send browser.js messages off the current
 * thread. These will mostly be scheduled from the UI thread and processed on
 * the main thread (inside browser.js).
 */

namespace mozilla {
namespace layers {

/**
 * Event for managing any viewport changes.
 */
class ViewportEvent : public nsRunnable {
public:
  ViewportEvent(const nsAString& aType, const nsAString& aData);
  NS_IMETHOD Run();

private:
  nsString mType;
  nsString mData;
};

/**
 * Event for managing gestures, such as taps and long presses.
 */
class GestureEvent : public nsRunnable {
public:
  GestureEvent(const nsAString& aTopic, const nsAString& aScrollOffset);
  NS_IMETHOD Run();

private:
  nsString mTopic;
  nsString mData;
};

}
}

#endif
