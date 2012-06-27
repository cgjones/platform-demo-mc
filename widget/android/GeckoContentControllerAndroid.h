/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_GeckoContentControllerAndroid_h
#define mozilla_layers_GeckoContentControllerAndroid_h

#include "mozilla/layers/GeckoContentController.h"

namespace mozilla {
namespace layers {

/**
 * Android-specific content controller. Overrides some methods that require
 * extra work on for this platform.
 */
class GeckoContentControllerAndroid : public GeckoContentController {
public:
  /**
   * Override tap gesture events and send an extra AndroidGeckoEvent to nsWindow
   * so that it can determine the target for a tap and open links, do
   * highlighting, etc.
   */
  void SendGestureEvent(const nsAString& aTopic,
                        const nsIntPoint& aPoint);
};

}
}

#endif // mozilla_layers_GeckoContentControllerAndroid_h
