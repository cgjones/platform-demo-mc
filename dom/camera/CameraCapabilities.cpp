/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>
#include <stdlib.h>
#include "nsDOMClassInfo.h"
#include "jsapi.h"
#include "camera/CameraParameters.h"
#include "CameraControl.h"
#include "CameraCapabilities.h"

#define DOM_CAMERA_LOG_LEVEL  3
#include "CameraCommon.h"


using namespace android;

#define CHECK_CAMERA_PTR(c)         \
  if (!c) {                         \
    return NS_ERROR_NOT_AVAILABLE;  \
  }

// NS_IMPL_ISUPPORTS1(nsCameraCapabilities, nsICameraCapabilities)

DOMCI_DATA(CameraCapabilities, nsICameraCapabilities)

NS_INTERFACE_MAP_BEGIN(nsCameraCapabilities)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsICameraCapabilities)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(CameraCapabilities)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(nsCameraCapabilities)
NS_IMPL_RELEASE(nsCameraCapabilities)


nsCameraCapabilities::nsCameraCapabilities(nsCameraControl *aCamera) :
  mCamera(aCamera)
{
  /* member initializers and constructor code */
  DOM_CAMERA_LOGI("%s:%d : this=%p\n", __func__, __LINE__, this);
}

nsCameraCapabilities::~nsCameraCapabilities()
{
  /* destructor code */
  // DOM_CAMERA_LOGI("%s:%d - |mCamera = nsnull| removed\n", __func__, __LINE__);
  DOM_CAMERA_LOGI("%s:%d : this=%p, mCamera=%p\n", __func__, __LINE__, this, mCamera.get());
  mCamera = nsnull;
  DOM_CAMERA_LOGI("%s:%d : this=%p\n", __func__, __LINE__, this);
}

static nsresult
parseZoomRatioItemAndAdd(JSContext *cx, JSObject *array, PRUint32 index, const char *start, char **end)
{
  if (!*end) {
    /* make 'end' follow the same semantics as strchr(). */
    end = nsnull;
  }

  double d = strtod(start, end);
  jsval v;

  d /= 100;
  if (!JS_NewNumberValue(cx, d, &v)) {
    return NS_ERROR_FAILURE;
  }
  if (!JS_SetElement(cx, array, index, &v)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

static nsresult
parseStringItemAndAdd(JSContext *cx, JSObject *array, PRUint32 index, const char *start, char **end)
{
  JSString* v;
  jsval jv;

  if (*end) {
    v = JS_NewStringCopyN(cx, start, *end - start);
  } else {
    v = JS_NewStringCopyZ(cx, start);
  }

  if (!v) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  jv = STRING_TO_JSVAL(v);
  if (!JS_SetElement(cx, array, index, &jv)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

static nsresult
parseDimensionItemAndAdd(JSContext *cx, JSObject *array, PRUint32 index, const char *start, char **end)
{
  char* x;
  jsval w;
  jsval h;
  jsval v;

  if (!*end) {
    /* make 'end' follow the same semantics as strchr(). */
    end = nsnull;
  }

  w = INT_TO_JSVAL(strtol(start, &x, 10));
  h = INT_TO_JSVAL(strtol(x + 1, end, 10));

  JSObject *o = JS_NewObject(cx, nsnull, nsnull, nsnull);
  if (!o) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!JS_SetProperty(cx, o, "width", &w)) {
    return NS_ERROR_FAILURE;
  }
  if (!JS_SetProperty(cx, o, "height", &h)) {
    return NS_ERROR_FAILURE;
  }

  v = OBJECT_TO_JSVAL(o);
  if (!JS_SetElement(cx, array, index, &v)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult
nsCameraCapabilities::parameterListToNewArray(JSContext *cx, JSObject **array, const char *key, nsresult (*parseItemAndAdd)(JSContext *cx, JSObject *array, PRUint32 index, const char *start,  char **end))
{
  CHECK_CAMERA_PTR(mCamera);

  PRUint32 index = 0;
  const char *value;
  nsresult rv;

  value = mCamera->GetParameter(key);
  if (!value) {
    /* in case we get nonsense data back */
    *array = nsnull;
    return NS_OK;
  }

  *array = JS_NewArrayObject(cx, 0, nsnull);

  const char* p = value;
  char* q;
  while (p) {
    q = strchr(p, ',');
    if (q != p) { /* skip consecutive delimiters, just in case */
      rv = parseItemAndAdd(cx, *array, index, p, &q);
      if (rv != NS_OK) {
        return rv;
      }
      index += 1;
    }
    p = q;
    if (p) {
      p += 1;
    }
  }

  return NS_OK;
}

nsresult
nsCameraCapabilities::stringListToNewObject(JSContext* cx, JS::Value *aArray, const char *key)
{
  JSObject* array;
  nsresult rv;

  rv = parameterListToNewArray(cx, &array, key, parseStringItemAndAdd);
  NS_ENSURE_SUCCESS(rv, rv);

  *aArray = OBJECT_TO_JSVAL(array);
  return NS_OK;
}

nsresult
nsCameraCapabilities::dimensionListToNewObject(JSContext* cx, JS::Value *aArray, const char *key)
{
  JSObject* array;
  nsresult rv;

  rv = parameterListToNewArray(cx, &array, key, parseDimensionItemAndAdd);
  NS_ENSURE_SUCCESS(rv, rv);

  *aArray = OBJECT_TO_JSVAL(array);
  return NS_OK;
}

/* readonly attribute jsval previewSizes; */
NS_IMETHODIMP
nsCameraCapabilities::GetPreviewSizes(JSContext* cx, JS::Value *aPreviewSizes)
{
  return dimensionListToNewObject(cx, aPreviewSizes, CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES);
}

/* readonly attribute jsval pictureSizes; */
NS_IMETHODIMP
nsCameraCapabilities::GetPictureSizes(JSContext* cx, JS::Value *aPictureSizes)
{
  return dimensionListToNewObject(cx, aPictureSizes, CameraParameters::KEY_SUPPORTED_PICTURE_SIZES);
}

/* readonly attribute jsval fileFormats; */
NS_IMETHODIMP
nsCameraCapabilities::GetFileFormats(JSContext* cx, JS::Value *aFileFormats)
{
  return stringListToNewObject(cx, aFileFormats, CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS);
}

/* readonly attribute jsval whiteBalanceModes; */
NS_IMETHODIMP
nsCameraCapabilities::GetWhiteBalanceModes(JSContext* cx, JS::Value *aWhiteBalanceModes)
{
  return stringListToNewObject(cx, aWhiteBalanceModes, CameraParameters::KEY_SUPPORTED_WHITE_BALANCE);
}

/* readonly attribute jsval sceneModes; */
NS_IMETHODIMP
nsCameraCapabilities::GetSceneModes(JSContext* cx, JS::Value *aSceneModes)
{
  return stringListToNewObject(cx, aSceneModes, CameraParameters::KEY_SUPPORTED_SCENE_MODES);
}

/* readonly attribute jsval effects; */
NS_IMETHODIMP
nsCameraCapabilities::GetEffects(JSContext* cx, JS::Value *aEffects)
{
  return stringListToNewObject(cx, aEffects, CameraParameters::KEY_SUPPORTED_EFFECTS);
}

/* readonly attribute jsval flashModes; */
NS_IMETHODIMP
nsCameraCapabilities::GetFlashModes(JSContext* cx, JS::Value *aFlashModes)
{
  return stringListToNewObject(cx, aFlashModes, CameraParameters::KEY_SUPPORTED_FLASH_MODES);
}

/* readonly attribute jsval focusModes; */
NS_IMETHODIMP
nsCameraCapabilities::GetFocusModes(JSContext* cx, JS::Value *aFocusModes)
{
  return stringListToNewObject(cx, aFocusModes, CameraParameters::KEY_SUPPORTED_FOCUS_MODES);
}

/* readonly attribute long maxFocusAreas; */
NS_IMETHODIMP
nsCameraCapabilities::GetMaxFocusAreas(JSContext* cx, PRInt32 *aMaxFocusAreas)
{
  CHECK_CAMERA_PTR(mCamera);

  const char* value = mCamera->GetParameter(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS);
  if (!value) {
    /* in case we get nonsense data back */
    *aMaxFocusAreas = 0;
    return NS_OK;
  }

  *aMaxFocusAreas = atoi(value);
  return NS_OK;
}

/* readonly attribute double minExposureCompensation; */
NS_IMETHODIMP
nsCameraCapabilities::GetMinExposureCompensation(JSContext* cx, double *aMinExposureCompensation)
{
  CHECK_CAMERA_PTR(mCamera);

  const char* value = mCamera->GetParameter(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);
  if (!value) {
    /* in case we get nonsense data back */
    *aMinExposureCompensation = 0;
    return NS_OK;
  }

  *aMinExposureCompensation = atof(value);
  return NS_OK;
}

/* readonly attribute double maxExposureCompensation; */
NS_IMETHODIMP
nsCameraCapabilities::GetMaxExposureCompensation(JSContext* cx, double *aMaxExposureCompensation)
{
  CHECK_CAMERA_PTR(mCamera);

  const char* value = mCamera->GetParameter(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
  if (!value) {
    /* in case we get nonsense data back */
    *aMaxExposureCompensation = 0;
    return NS_OK;
  }

  *aMaxExposureCompensation = atof(value);
  return NS_OK;
}

/* readonly attribute double stepExposureCompensation; */
NS_IMETHODIMP
nsCameraCapabilities::GetStepExposureCompensation(JSContext* cx, double *aStepExposureCompensation)
{
  CHECK_CAMERA_PTR(mCamera);

  const char* value = mCamera->GetParameter(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP);
  if (!value) {
    /* in case we get nonsense data back */
    *aStepExposureCompensation = 0;
    return NS_OK;
  }

  *aStepExposureCompensation = atof(value);
  return NS_OK;
}

/* readonly attribute long maxMeteringAreas; */
NS_IMETHODIMP
nsCameraCapabilities::GetMaxMeteringAreas(JSContext* cx, PRInt32 *aMaxMeteringAreas)
{
  CHECK_CAMERA_PTR(mCamera);

  const char* value = mCamera->GetParameter(CameraParameters::KEY_MAX_NUM_METERING_AREAS);
  if (!value) {
    /* in case we get nonsense data back */
    *aMaxMeteringAreas = 0;
    return NS_OK;
  }

  *aMaxMeteringAreas = atoi(value);
  return NS_OK;
}

/* readonly attribute jsval zoomRatios; */
NS_IMETHODIMP
nsCameraCapabilities::GetZoomRatios(JSContext* cx, JS::Value *aZoomRatios)
{
  CHECK_CAMERA_PTR(mCamera);

  const char* value;
  JSObject* array;
  nsresult rv;

  value = mCamera->GetParameter(CameraParameters::KEY_ZOOM_SUPPORTED);
  if (!value || strcmp(value, CameraParameters::TRUE) != 0) {
    /* if zoom is not supported, return a null object */
    *aZoomRatios = JSVAL_NULL;
    return NS_OK;
  }

  rv = parameterListToNewArray(cx, &array, CameraParameters::KEY_ZOOM_RATIOS, parseZoomRatioItemAndAdd);
  NS_ENSURE_SUCCESS(rv, rv);

  *aZoomRatios = OBJECT_TO_JSVAL(array);
  return NS_OK;
}

/* readonly attribute jsval videoSizes; */
NS_IMETHODIMP
nsCameraCapabilities::GetVideoSizes(JSContext* cx, JS::Value *aVideoSizes)
{
  return dimensionListToNewObject(cx, aVideoSizes, CameraParameters::KEY_SUPPORTED_VIDEO_SIZES);
}
