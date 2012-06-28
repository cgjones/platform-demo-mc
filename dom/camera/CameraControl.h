/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_CAMERA_NSCAMERACONTROL_H
#define DOM_CAMERA_NSCAMERACONTROL_H


// #include "utils/StrongPointer.h"
#include "camera/CameraParameters.h"
#include "prtypes.h"
#include "prrwlock.h"
#include "nsCOMPtr.h"
#include "nsThread.h"
#include "nsDOMFile.h"
#include "CameraPreview.h"
#include "nsIDOMCameraManager.h"

#define DOM_CAMERA_LOG_LEVEL 3
#include "CameraCommon.h"


class GetPreviewStreamTask;
class AutoFocusTask;
class TakePictureTask;
class StartRecordingTask;
class StopRecordingTask;
class SetParameterTask;
class GetParameterTask;
class PushParametersTask;
class PullParametersTask;

/*
  Main camera control.
*/
class nsCameraControl : public nsICameraControl
{
  friend class GetPreviewStreamTask;
  friend class AutoFocusTask;
  friend class TakePictureTask;
  friend class StartRecordingTask;
  friend class StopRecordingTask;
  friend class SetParameterTask;
  friend class GetParameterTask;
  friend class PushParametersTask;
  friend class PullParametersTask;

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICAMERACONTROL

  class CameraRegion
  {
  public:
    PRInt32 mTop;
    PRInt32 mLeft;
    PRInt32 mBottom;
    PRInt32 mRight;
    PRUint32 mWeight;
  };

  enum {
    CAMERA_PARAM_EFFECT,
    CAMERA_PARAM_WHITEBALANCE,
    CAMERA_PARAM_SCENEMODE,
    CAMERA_PARAM_FLASHMODE,
    CAMERA_PARAM_FOCUSMODE,
    CAMERA_PARAM_ZOOM,
    CAMERA_PARAM_METERINGAREAS,
    CAMERA_PARAM_FOCUSAREAS,
    CAMERA_PARAM_FOCALLENGTH,
    CAMERA_PARAM_FOCUSDISTANCENEAR,
    CAMERA_PARAM_FOCUSDISTANCEOPTIMUM,
    CAMERA_PARAM_FOCUSDISTANCEFAR,
    CAMERA_PARAM_EXPOSURECOMPENSATION
  };
  const char* GetParameter(const char *aKey);
  const char* GetParameterConstChar(PRUint32 aKey);
  double GetParameterDouble(PRUint32 aKey);
  void GetParameter(PRUint32 aKey, CameraRegion **aRegions, PRUint32 *aLength);
  void SetParameter(const char *aKey, const char *aValue);
  void SetParameter(PRUint32 aKey, const char *aValue);
  void SetParameter(PRUint32 aKey, double aValue);
  void SetParameter(PRUint32 aKey, CameraRegion *aRegions, PRUint32 aLength);
  void PushParameters();

  void TakePictureComplete(PRUint8 *aData, PRUint32 aLength);
  void AutoFocusComplete(bool aSuccess);
  void ReceiveFrame(PRUint8 *aData, PRUint32 aLength);

  nsCameraControl(PRUint32 aCameraId, nsIThread *aCameraThread);
  ~nsCameraControl();

protected:
  nsresult DoGetPreviewStream(GetPreviewStreamTask *aGetPreviewStream);
  nsresult DoAutoFocus(AutoFocusTask *aAutoFocus);
  nsresult DoTakePicture(TakePictureTask *aTakePicture);
  nsresult DoStartRecording(StartRecordingTask *aStartRecording);
  nsresult DoStopRecording(StopRecordingTask *aStopRecording);
  nsresult DoPushParameters(PushParametersTask *aPushParameters);
  nsresult DoPullParameters(PullParametersTask *aPullParameters);

private:
  nsCameraControl(const nsCameraControl&);

protected:
  /* additional members */
  PRUint32                        mCameraId;
  nsCOMPtr<nsIThread>             mCameraThread;
  nsCOMPtr<nsICameraCapabilities> mCapabilities;
  PRUint32                        mHwHandle;
  PRUint32                        mPreviewWidth;
  PRUint32                        mPreviewHeight;
  nsCOMPtr<CameraPreview>         mPreview;
  const char*                     mFileFormat;
  bool                            mDeferConfigUpdate;

  nsCOMPtr<nsICameraAutoFocusCallback>      mAutoFocusOnSuccessCb;
  nsCOMPtr<nsICameraErrorCallback>          mAutoFocusOnErrorCb;
  nsCOMPtr<nsICameraTakePictureCallback>    mTakePictureOnSuccessCb;
  nsCOMPtr<nsICameraErrorCallback>          mTakePictureOnErrorCb;
  nsCOMPtr<nsICameraStartRecordingCallback> mStartRecordingOnSuccessCb;
  nsCOMPtr<nsICameraErrorCallback>          mStartRecordingOnErrorCb;
  nsCOMPtr<nsICameraShutterCallback>        mOnShutterCb;

  /* TODO: move this into a Gonk-specific class */
  android::CameraParameters       mParams;
  PRRWLock*                       mRwLock;
};

/*
  Return the resulting preview stream to JS.  Runs on the main thread.
*/
class GetPreviewStreamResult : public nsRunnable
{
public:
  GetPreviewStreamResult(nsIDOMMediaStream *aStream, nsICameraPreviewStreamCallback *onSuccess)
    : mStream(aStream)
    , mOnSuccessCb(onSuccess)
  { }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());

    if (mOnSuccessCb) {
      mOnSuccessCb->HandleEvent(mStream);
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsIDOMMediaStream> mStream;
  nsCOMPtr<nsICameraPreviewStreamCallback> mOnSuccessCb;
};

/*
  Get the desired preview stream.
*/
class GetPreviewStreamTask : public nsRunnable
{
  friend class nsCameraControl;

public:
  GetPreviewStreamTask(nsCameraControl *aCameraControl, PRUint32 aWidth, PRUint32 aHeight, nsICameraPreviewStreamCallback *onSuccess, nsICameraErrorCallback *onError)
    : mWidth(aWidth)
    , mHeight(aHeight)
    , mCameraControl(aCameraControl)
    , mOnSuccessCb(onSuccess)
    , mOnErrorCb(onError)
  { }

  NS_IMETHOD Run()
  {
    nsresult rv = mCameraControl->DoGetPreviewStream(this);

    if (NS_FAILED(rv)) {
      if (NS_FAILED(NS_DispatchToMainThread(new CameraErrorResult(mOnErrorCb, NS_LITERAL_STRING("FAILURE"))))) {
        NS_WARNING("Failed to dispatch getPreviewStream() onError callback to main thread!");
      }
    }
    return NS_OK;
  }

protected:
  PRUint32 mWidth;
  PRUint32 mHeight;
  nsCOMPtr<nsCameraControl> mCameraControl;
  nsCOMPtr<nsICameraPreviewStreamCallback> mOnSuccessCb;
  nsCOMPtr<nsICameraErrorCallback> mOnErrorCb;
};

/*
  Return the autofocus status to JS.  Runs on the main thread.
*/
class AutoFocusResult : public nsRunnable
{
public:
  AutoFocusResult(bool aSuccess, nsICameraAutoFocusCallback *onSuccess)
    : mSuccess(aSuccess)
    , mOnSuccessCb(onSuccess)
  { }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());

    if (mOnSuccessCb) {
      mOnSuccessCb->HandleEvent(mSuccess);
    }
    return NS_OK;
  }

protected:
  bool mSuccess;
  nsCOMPtr<nsICameraAutoFocusCallback> mOnSuccessCb;
};

/*
  Autofocus the camera.
*/
class AutoFocusTask : public nsRunnable
{
  friend class nsCameraControl;

public:
  AutoFocusTask(nsCameraControl *aCameraControl, nsICameraAutoFocusCallback *onSuccess, nsICameraErrorCallback *onError)
    : mCameraControl(aCameraControl)
    , mOnSuccessCb(onSuccess)
    , mOnErrorCb(onError)
  { }

  NS_IMETHOD Run()
  {
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);
    nsresult rv = mCameraControl->DoAutoFocus(this);
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);

    if (NS_FAILED(rv)) {
      if (NS_FAILED(NS_DispatchToMainThread(new CameraErrorResult(mOnErrorCb, NS_LITERAL_STRING("FAILURE"))))) {
        NS_WARNING("Failed to dispatch takePicture() onError callback to main thread!");
      }
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsCameraControl> mCameraControl;
  nsCOMPtr<nsICameraAutoFocusCallback> mOnSuccessCb;
  nsCOMPtr<nsICameraErrorCallback> mOnErrorCb;
};

/*
  Return the captured picture to JS.  Runs on the main thread.
*/
class TakePictureResult : public nsRunnable
{
public:
  TakePictureResult(nsIDOMBlob *aImage, nsICameraTakePictureCallback *onSuccess)
    : mImage(aImage)
    , mOnSuccessCb(onSuccess)
  { }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());

    if (mOnSuccessCb) {
      mOnSuccessCb->HandleEvent(mImage);
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsIDOMBlob> mImage;
  nsCOMPtr<nsICameraTakePictureCallback> mOnSuccessCb;
};

/*
  Capture a still image with the camera.
*/
class TakePictureTask : public nsRunnable
{
  friend class nsCameraControl;

public:
  TakePictureTask(nsCameraControl *aCameraControl, PRUint32 aWidth, PRUint32 aHeight, PRInt32 aRotation, nsString aFileFormat, double aLatitude, bool aLatitudeSet, double aLongitude, bool aLongitudeSet, double aAltitude, bool aAltitudeSet, double aTimestamp, bool aTimestampSet, nsICameraTakePictureCallback *onSuccess, nsICameraErrorCallback *onError)
    : mCameraControl(aCameraControl)
    , mWidth(aWidth)
    , mHeight(aHeight)
    , mRotation(aRotation)
    , mFileFormat(aFileFormat)
    , mLatitude(aLatitude)
    , mLatitudeSet(aLatitudeSet)
    , mLongitude(aLongitude)
    , mLongitudeSet(aLongitudeSet)
    , mAltitude(aAltitude)
    , mAltitudeSet(aAltitudeSet)
    , mTimestamp(aTimestamp)
    , mTimestampSet(aTimestampSet)
    , mOnSuccessCb(onSuccess)
    , mOnErrorCb(onError)
  { }

  NS_IMETHOD Run()
  {
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);
    nsresult rv = mCameraControl->DoTakePicture(this);
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);

    if (NS_FAILED(rv)) {
      if (NS_FAILED(NS_DispatchToMainThread(new CameraErrorResult(mOnErrorCb, NS_LITERAL_STRING("FAILURE"))))) {
        NS_WARNING("Failed to dispatch takePicture() onError callback to main thread!");
      }
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsCameraControl> mCameraControl;
  PRUint32 mWidth;
  PRUint32 mHeight;
  PRInt32 mRotation;
  nsString mFileFormat;
  double mLatitude;
  bool mLatitudeSet;
  double mLongitude;
  bool mLongitudeSet;
  double mAltitude;
  bool mAltitudeSet;
  double mTimestamp;
  bool mTimestampSet;
  nsCOMPtr<nsICameraTakePictureCallback> mOnSuccessCb;
  nsCOMPtr<nsICameraErrorCallback> mOnErrorCb;
};

/*
  Return the captured video to JS.  Runs on the main thread.
*/
class StartRecordingResult : public nsRunnable
{
public:
  StartRecordingResult(nsIDOMMediaStream *aStream, nsICameraStartRecordingCallback *onSuccess)
    : mStream(aStream)
    , mOnSuccessCb(onSuccess)
  { }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());

    if (mOnSuccessCb) {
      mOnSuccessCb->HandleEvent(mStream);
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsIDOMMediaStream> mStream;
  nsCOMPtr<nsICameraStartRecordingCallback> mOnSuccessCb;
};

/*
  Start video recording.
*/
class StartRecordingTask : public nsRunnable
{
  friend class nsCameraControl;

public:
  StartRecordingTask(nsCameraControl *aCameraControl, PRUint32 aWidth, PRUint32 aHeight, nsICameraStartRecordingCallback *onSuccess, nsICameraErrorCallback *onError)
    : mWidth(aWidth)
    , mHeight(aHeight)
    , mCameraControl(aCameraControl)
    , mOnSuccessCb(onSuccess)
    , mOnErrorCb(onError)
  { }

  NS_IMETHOD Run()
  {
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);
    nsresult rv = mCameraControl->DoStartRecording(this);
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);

    if (NS_FAILED(rv)) {
      if (NS_FAILED(NS_DispatchToMainThread(new CameraErrorResult(mOnErrorCb, NS_LITERAL_STRING("FAILURE"))))) {
        NS_WARNING("Failed to dispatch startRecording() onError callback to main thread!");
      }
    }
    return NS_OK;
  }

protected:
  PRUint32 mWidth;
  PRUint32 mHeight;
  nsCOMPtr<nsCameraControl> mCameraControl;
  nsCOMPtr<nsICameraStartRecordingCallback> mOnSuccessCb;
  nsCOMPtr<nsICameraErrorCallback> mOnErrorCb;
};

/*
  Stop video recording.
*/
class StopRecordingTask : public nsRunnable
{
  friend class nsCameraControl;

public:
  StopRecordingTask(nsCameraControl *aCameraControl)
    : mCameraControl(aCameraControl)
  { }

  NS_IMETHOD Run()
  {
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);
    nsresult rv = mCameraControl->DoStopRecording(this);
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);

    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to dispatch stopRecording()!");
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsCameraControl> mCameraControl;
};

/*
  Pushes all camera parameters to the camera.
*/
class PushParametersTask : public nsRunnable
{
  friend class nsCameraControl;

public:
  PushParametersTask(nsCameraControl *aCameraControl)
    : mCameraControl(aCameraControl)
  { }

  NS_IMETHOD Run()
  {
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);
    nsresult rv = mCameraControl->DoPushParameters(this);
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);

    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to dispatch setParameter() to camera!");
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsCameraControl> mCameraControl;
};

/*
  Get all camera parameters from the camera.
*/
class PullParametersTask : public nsRunnable
{
  friend class nsCameraControl;

public:
  PullParametersTask(nsCameraControl *aCameraControl)
    : mCameraControl(aCameraControl)
  { }

  NS_IMETHOD Run()
  {
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);
    nsresult rv = mCameraControl->DoPullParameters(this);
    DOM_CAMERA_LOGI("%s:%d\n", __func__, __LINE__);

    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to dispatch getParameter() to camera!");
    }
    return NS_OK;
  }

protected:
  nsCOMPtr<nsCameraControl> mCameraControl;
};


#endif // DOM_CAMERA_NSCAMERACONTROL_H
