/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMCameraManager.h"


/*
  From nsDOMCameraManager.
*/

/* [implicit_jscontext] void getCamera ([optional] in jsval aOptions, in nsICameraGetCameraCallback onSuccess, [optional] in nsICameraErrorCallback onError); */
NS_IMETHODIMP
nsDOMCameraManager::GetCamera(const JS::Value & aOptions, nsICameraGetCameraCallback *onSuccess, nsICameraErrorCallback *onError, JSContext* cx)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* [implicit_jscontext] jsval getListOfCameras (); */
NS_IMETHODIMP
nsDOMCameraManager::GetListOfCameras(JSContext* cx, JS::Value *_retval NS_OUTPARAM)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}
