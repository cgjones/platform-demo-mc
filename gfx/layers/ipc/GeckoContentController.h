/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_GeckoContentController_h
#define mozilla_layers_GeckoContentController_h

#include "gfxTypes.h"
#include "gfxASurface.h"
#include "gfx3DMatrix.h"
#include "mozilla/gfx/2D.h"
#include "Layers.h"

namespace mozilla {
namespace layers {

class GeckoContentController {
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GeckoContentController)

  virtual ~GeckoContentController() { }

  /**
   * XXX: Rename this to "SendFrameMetrics" or something similar. Unfortunately,
   * it must be left this way because the message being sent is called
   * "Viewport:Change". Once we remove the browser.js code, we can rename this
   * to something more accurate.
   *
   * Sends updated frame metrics to Gecko for it to repaint. This gets
   * dispatched to the main thread.
   */
  virtual void SendViewportChange(const FrameMetrics& aFrameMetrics);

  /**
   * Sends a gesture event to browser.js so that it can handle opening links,
   * etc. This gets dispatched to the main thread.
   */
  virtual void SendGestureEvent(const nsAString& aTopic,
                                const nsIntPoint& aPoint);
};

}
}

#endif // mozilla_layers_GeckoContentController_h
