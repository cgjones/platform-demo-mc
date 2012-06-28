/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_CAMERA_CAMERACOMMON_H
#define DOM_CAMERA_CAMERACOMMON_H


#include "nsThreadUtils.h"
#include "nsIDOMCameraManager.h"


#define BEGIN_CAMERA_NAMESPACE \
  namespace mozilla { namespace dom { namespace camera {

#define END_CAMERA_NAMESPACE \
  } /* namespace camera */ } /* namespace dom */ } /* namespace mozilla */

#define USING_CAMERA_NAMESPACE \
  using namespace mozilla::dom::camera;


#define DOM_CAMERA_LOG( l, ... )       \
  do {                                  \
    if ( DOM_CAMERA_LOG_LEVEL >= (l) ) {  \
      printf_stderr (__VA_ARGS__); \
    }                                   \
  } while (0)

#define DOM_CAMERA_LOGA( ... )        DOM_CAMERA_LOG( 0, __VA_ARGS__ )

#if DOM_CAMERA_LOG_LEVEL
enum {
  DOM_CAMERA_LOG_NOTHING,
  DOM_CAMERA_LOG_ERROR,
  DOM_CAMERA_LOG_WARNING,
  DOM_CAMERA_LOG_INFO
};

#define DOM_CAMERA_LOGI( ... )        DOM_CAMERA_LOG( DOM_CAMERA_LOG_INFO,  __VA_ARGS__ )
#define DOM_CAMERA_LOGW( ... )        DOM_CAMERA_LOG( DOM_CAMERA_LOG_WARNING, __VA_ARGS__ )
#define DOM_CAMERA_LOGE( ... )        DOM_CAMERA_LOG( DOM_CAMERA_LOG_ERROR, __VA_ARGS__ )
#else
#define DOM_CAMERA_LOGI( ... )
#define DOM_CAMERA_LOGW( ... )
#define DOM_CAMERA_LOGE( ... )
#endif


class CameraErrorResult : public nsRunnable
{
public:
  CameraErrorResult(nsICameraErrorCallback *onError, const nsString& aErrorMsg)
    : mOnErrorCb(onError)
    , mErrorMsg(aErrorMsg)
  { }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());

    if (mOnErrorCb) {
      mOnErrorCb->HandleEvent(mErrorMsg);
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsICameraErrorCallback> mOnErrorCb;
  const nsString mErrorMsg;
};


#endif // DOM_CAMERA_CAMERACOMMON_H
