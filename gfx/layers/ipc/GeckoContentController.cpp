/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoContentController.h"
#include "nsXPCOMStrings.h"
#include "nsPrintfCString.h"
#include "CompositeEvent.h"
#include "AsyncPanZoomController.h"

namespace mozilla {
namespace layers {

void GeckoContentController::SendViewportChange(const FrameMetrics& aFrameMetrics,
                                                const nsIntRect& aDisplayPort) {
  nsCString data;
  // XXX: When we start removing browser.js code, we can stop doing weird
  // stringifying like this.
  data += nsPrintfCString("{ \"x\" : %d", aFrameMetrics.mViewportScrollOffset.x);
  data += nsPrintfCString(", \"y\" : %d", aFrameMetrics.mViewportScrollOffset.y);
  // We don't treat the x and y scales any differently for this
  // semi-platform-specific code.
  data += nsPrintfCString(", \"zoom\" : %f", aFrameMetrics.mResolution.width);
  data += nsPrintfCString(", \"displayPort\" : ");
    data += nsPrintfCString("{ \"left\" : %d", aDisplayPort.X());
    data += nsPrintfCString(", \"top\" : %d", aDisplayPort.Y());
    data += nsPrintfCString(", \"right\" : %d", aDisplayPort.XMost());
    data += nsPrintfCString(", \"bottom\" : %d", aDisplayPort.YMost());
    data += nsPrintfCString(", \"resolution\" : %f", aFrameMetrics.mResolution.width);
    data += nsPrintfCString(" }");
  data += nsPrintfCString(" }");
  nsCOMPtr<nsIRunnable> viewportEvent = new ViewportEvent(
    NS_LITERAL_STRING("Viewport:Change"), NS_ConvertUTF8toUTF16(data)
  );
  NS_DispatchToMainThread(viewportEvent);
}

void GeckoContentController::SendGestureEvent(const nsAString& aTopic,
                                              const nsIntPoint& aPoint) {
  nsCString data;
  data += nsPrintfCString("{ \"x\" : %d", aPoint.x);
  data += nsPrintfCString(", \"y\" : %d }", aPoint.y);
  nsCOMPtr<nsIRunnable> gestureEvent =
    new GestureEvent(aTopic, NS_ConvertUTF8toUTF16(data));
  NS_DispatchToMainThread(gestureEvent);
}

}
}
