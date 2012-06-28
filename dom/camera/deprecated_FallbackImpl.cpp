/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "nsDOMMediaStream.h"
#include "cameracontrol.h"
#include "CameraImpl.h"
#include "nsITimer.h"
#include "MediaStreamGraph.h"
#include "BaseCameraStream.h"
#include "VideoUtils.h"
#include "StreamBuffer.h"

using namespace mozilla;
using namespace mozilla::layers;

static const TrackID TRACK_AUDIO = 1;
static const TrackID TRACK_VIDEO = 2;

class FallbackCamera : public nsICameraControl
                     , public nsDOMMediaStream
                     , public nsITimerCallback
                     , public BaseCameraStream
                     , public MediaStreamListener
{
public:
  NS_DECL_ISUPPORTS

  FallbackCamera(PRUint32 aWidth, PRUint32 aHeight, PRUint32 aFps) :
  nsDOMMediaStream(), mWidth(aWidth), mHeight(aHeight), mFps(aFps),
  mMonitor("FallbackCamera.Monitor"), mNumFrames(0)
  {
    printf_stderr("XxXxX FallbackCamera::FallbackCamera\n");
    nsresult rv;

    init();

    mTimer = do_CreateInstance("@mozilla.org/timer;1", &rv);
    if (NS_SUCCEEDED(rv)) {
      mTimer->InitWithCallback(this, 1000 / mFps, nsITimer::TYPE_REPEATING_SLACK);
    }
  }

  ~FallbackCamera()
  {
    printf_stderr("XxXxX FallbackCamera::~FallbackCamera\n");
    mStream->RemoveListener(this);
    mTimer->Cancel();
    mTimer = nsnull;
  }

  void init() {
    printf_stderr("XxXxX init()\n");
    mImageContainer = LayerManager::CreateImageContainer();
    MediaStreamGraph* gm = MediaStreamGraph::GetInstance();
    mStream = gm->CreateInputStream(this);
    mStream->AddListener(this);
    mInput = GetStream()->AsSourceStream();
    PRUint32 rate = mWidth * mHeight * 12 / 8 * mFps;
    mInput->AddTrack(TRACK_VIDEO, mFps, 0, new VideoSegment());
    mInput->AdvanceKnownTracksTime(MEDIA_TIME_MAX);
  }

  //
  // MediaStreamListener
  //
  void NotifyBlockingChanged(MediaStreamGraph* aGraph, Blocking aBlocked) {
    printf_stderr("XxXxX NotifyBlockingChanged blocked=%d\n", aBlocked);
  }

  void NotifyOutput(MediaStreamGraph* aGraph) {
    printf_stderr("XxXxX NotifyOutput\n");
  }

  void NotifyFinished(MediaStreamGraph* aGraph) {
    printf_stderr("XxXxX NotifyFinished\n");
  }

  void NotifyQueuedTrackChanges(MediaStreamGraph* aGraph, TrackID aID,
                                TrackRate aTrackRate,
                                TrackTicks aTrackOffset,
                                PRUint32 aTrackEvents,
                                const MediaSegment& aQueuedMedia) {
    printf_stderr("XxXxX NotifyQueuedTrackChanges rate=%d offset=%d event=%d\n", aTrackRate, aTrackOffset, aTrackEvents);
  }
  
  void NotifyConsumptionChanged(MediaStreamGraph* aGraph, Comsumption aConsumption) {
    printf_stderr("XxXxX NotifyConsumptionChanged consumption=%d\n", aConsumption);
  }

  /* nsIDOMDOMRequest autoFocus (); */
  NS_IMETHODIMP
  AutoFocus(nsIDOMDOMRequest * *_retval NS_OUTPARAM)
  {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /* nsIDOMDOMRequest takePicture (); */
  NS_IMETHODIMP
  TakePicture(nsIDOMDOMRequest * *_retval NS_OUTPARAM)
  {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHODIMP
  GetCurrentTime(double* aCurrentTime) {
    return nsDOMMediaStream::GetCurrentTime(aCurrentTime);
  }

  // nsITimerCallback
  NS_IMETHOD
  Notify(nsITimer *timer) {
    ReentrantMonitorAutoEnter enter(mMonitor);
    printf_stderr("XxXxX Notify\n");

    // create a VideoFrame and push it to the track
    Image::Format format = Image::PLANAR_YCBCR;
    nsRefPtr<Image> image = mImageContainer->CreateImage(&format, 1);
    PlanarYCbCrImage* videoImage = static_cast<PlanarYCbCrImage*> (image.get());

    PRUint32 length = mWidth * mHeight * 12 / 8;
    PRUint8* frame = (PRUint8*)moz_malloc(length);
    for (PRUint32 i = 0; i < length; i++) {
      frame[i] = random() % 0xff;
    }

    const PRUint8 lumaBpp = 8;
    const PRUint8 chromaBpp = 4;
    PlanarYCbCrImage::Data data;
    data.mYChannel = frame;
    data.mYSize = gfxIntSize(mWidth, mHeight);
    data.mYStride = mWidth * lumaBpp / 8.0;
    data.mCbCrStride = mWidth * chromaBpp / 8.0;
    data.mCbChannel = frame + mHeight * data.mYStride;
    data.mCrChannel = data.mCbChannel + mHeight * data.mCbCrStride / 2;
    data.mCbCrSize = gfxIntSize(mWidth / 2, mHeight / 2);
    data.mPicX = 0;
    data.mPicY = 0;
    data.mPicSize = gfxIntSize(mWidth, mHeight);
    data.mStereoMode = mozilla::layers::STEREO_MODE_MONO;
    videoImage->SetData(data); // Copies buffer

    moz_free(frame);

    mNumFrames++;
    mVideoSegment.AppendFrame(videoImage, 1, gfxIntSize(mWidth, mHeight));
    mInput->AppendToTrack(TRACK_VIDEO, &mVideoSegment);
    //PRUint32 time = SecondsToMediaTime(1 + mNumFrames / mFps);
    //mInput->AdvanceKnownTracksTime(time);
    return NS_OK;
  }

protected:
  PRUint32 mWidth;
  PRUint32 mHeight;
  PRUint32 mFps;
  SourceMediaStream* mInput;
  nsRefPtr<mozilla::layers::ImageContainer> mImageContainer;
  nsCOMPtr<nsITimer> mTimer;
  VideoSegment mVideoSegment;
  mozilla::ReentrantMonitor mMonitor;
  PRUint32 mNumFrames;
};

NS_IMPL_THREADSAFE_ISUPPORTS4(FallbackCamera, nsIDOMMediaStream, nsICameraControl, nsITimerCallback, nsIClassInfo)

PRInt32
CameraImpl::getNumberOfCameras()
{
  return 1;
}

NS_IMETHODIMP
CameraImpl::GetCamera(const JS::Value & aOptions, JSContext* cx, nsIDOMMediaStream * *_retval NS_OUTPARAM)
{
  printf_stderr("XxXxX CameraImpl::GetCamera()\n");
/*
  PRUint32 width = 480;
  PRUint32 height = 320;
  PRUint32 fps = 10;

  if (aOptions.isObject()) {
    // extract values from aOptions
    JSObject *options = JSVAL_TO_OBJECT(aOptions);
    jsval v;
    if (JS_GetProperty(cx, options, "camera", &v)) {
      PRInt32 camera;
      if (JSVAL_IS_INT(v)) {
        camera = JSVAL_TO_INT(v);
      }
      if (camera != 0)
        return NS_ERROR_FAILURE;
    }
    if (JS_GetProperty(cx, options, "width", &v)) {
      if (JSVAL_IS_INT(v))
        width = JSVAL_TO_INT(v);
    }
    if (JS_GetProperty(cx, options, "height", &v)) {
      if (JSVAL_IS_INT(v))
        height = JSVAL_TO_INT(v);
    }
    if (JS_GetProperty(cx, options, "fps", &v)) {
      if (JSVAL_IS_INT(v))
        fps = JSVAL_TO_INT(v);
    }
  }
  printf_stderr("XxXxX settings %dx%d @ %d fps\n", width, height, fps);
  nsCOMPtr<nsIDOMMediaStream> stream = new FallbackCamera(width, height, fps);
  *_retval = stream.get();
  NS_ADDREF(stream);
*/
  return NS_OK;
}
