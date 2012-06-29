/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_CAMERA_CAMERAPREVIEW_H
#define DOM_CAMERA_CAMERAPREVIEW_H


#include "MediaStreamGraph.h"
#include "StreamBuffer.h"
#include "nsDOMMediaStream.h"


using namespace mozilla;
using namespace mozilla::layers;


class CameraPreview : public nsDOMMediaStream
                    , public MediaStreamListener
{
public:
  NS_DECL_ISUPPORTS

  CameraPreview(PRUint32 aHwHandle, PRUint32 aWidth, PRUint32 aHeight);
  ~CameraPreview();

  NS_IMETHODIMP
  GetCurrentTime(double* aCurrentTime) {
    return nsDOMMediaStream::GetCurrentTime(aCurrentTime);
  }

  void ReceiveFrame(PRUint8 *aData, PRUint32 aLength);

  void Start();
  void Stop();

protected:
  PRUint32 mHwHandle;
  PRUint32 mWidth;
  PRUint32 mHeight;
  PRUint32 mFramesPerSecond;
  bool mIs420p;
  SourceMediaStream *mInput;
  nsRefPtr<mozilla::layers::ImageContainer> mImageContainer;
  VideoSegment mVideoSegment;
  PRUint32 mFrameCount;
  PRUint32 mDiscardedFrameCount;
};


#endif // DOM_CAMERA_CAMERAPREVIEW_H
