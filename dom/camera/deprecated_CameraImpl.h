/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef B2G_CAMERAIMPL_H
#define B2G_CAMERAIMPL_H

#include "nsComponentManagerUtils.h"
#include "nsIClassInfo.h"
#include "nsIProgrammingLanguage.h"
#include "nsIClassInfoImpl.h"

class CameraImpl {
public:
  static NS_IMETHODIMP getListOfCameras(JSContext* cx, JS::Value *_retval NS_OUTPARAM);
  static NS_IMETHODIMP GetCamera(const JS::Value & aOptions, nsICameraGetCameraCallback* onSuccess, nsICameraErrorCallback* onError, JSContext* cx);
};

#endif
