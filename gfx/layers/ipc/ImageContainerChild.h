/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGECONTAINERCHILD_H
#define MOZILLA_GFX_IMAGECONTAINERCHILD_H

#include "mozilla/layers/PImageContainerChild.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "ImageLayers.h"

namespace mozilla {
namespace layers {

class ImageBridgeCopyAndSendTask;


class ImageContainerChild : public PImageContainerChild
{
  friend class ImageBridgeChild;
  friend class ImageContainerChildDestroyTask;
  friend class ImageBridgeCopyAndSendTask;

public:
  ImageContainerChild(ImageBridgeChild* aBridge, ImageContainer* aContainer)
  : mBridge(aBridge), mImageID(0), mImageContainer(aContainer), mStop(false)
  , mActiveImageCount(0) {}

  virtual ~ImageContainerChild();

  /**
   * Sends an image to the compositor without using the main thread.
   *
   * This method should be called by ImageContainer only, and can be called 
   * from any thread.
   */
  void SendImageAsync(ImageContainer* aContainer, Image* aImage);

  
  /**
   *  Returns true if the method is called in the ImagebridgeChild thread.
   */
  inline bool InImageBridgeChildThread() const
  {
    return mBridge->InImageBridgeChildThread();
  }

  /**
   * Each ImageContainerChild is associated to an ID. This ID is used in the 
   * compositor side to fetch the images without having to keep direct references
   * between the ShadowImageLayer and the InmageBridgeParent.
   */
  const PRUint64& GetImageID() const
  {
    return mImageID;
  }

  /**
   * Overriden from PImageContainerChild.
   *
   * This methid is called whenever upon reception of an image that is not used by
   * the compositor anymore. When receiving shared images, the ImageContainer should 
   * store them in a pool to reuse them without reallocating shared memory when 
   * possible, or deallocate them if it can't keep them.
   */
  bool RecvReleasedSharedImage(const SharedImage& aImage);

  /**
   * Dispatches a task to the ImageBridge Thread, that will destroy the 
   * ImageContainerChild and associated imageContainerParent asynchonously.
   */
  void Destroy();

protected:

  virtual PGrallocBufferChild*
  AllocPGrallocBuffer(const gfxIntSize&, const gfxContentType&,
                      MaybeMagicGrallocBufferHandle*) MOZ_OVERRIDE;

  virtual bool
  DeallocPGrallocBuffer(PGrallocBufferChild* actor) MOZ_OVERRIDE;

  inline ImageBridgeChild* GetImageBridgeChild() const
  {
    return mBridge; 
  }

  inline MessageLoop* GetMessageLoop() const 
  {
    return mBridge->GetMessageLoop();
  }

  void DestroyNow();

  inline void SetImageID(PRUint64 id)
  {
    mImageID = id;
  }

  bool AllocBuffer(const gfxIntSize& aSize,
                   gfxASurface::gfxContentType aContent,
                   gfxSharedImageSurface** aBuffer);

  bool AllocSharedImage(const gfxIntSize& aSize,
                        gfxASurface::gfxContentType aContent,
                        SharedImage& aImage);

  SharedImage * CreateSharedImageFromData(Image* aImage);

  bool CopyDataIntoSharedImage(Image* src, SharedImage* dest);

  void DestroySharedImage(const SharedImage& aSurface);

  SharedImage* ImageToSharedImage(Image* toCopy);

  bool AddSharedImageToPool(SharedImage* img);
  SharedImage* PopSharedImageFromPool();
  void ClearSharedImagePool();

private:

  ImageBridgeChild* mBridge;
  PRUint64 mImageID;
  ImageContainer* mImageContainer;
  nsIntSize mSize;
  nsTArray<SharedImage*> mSharedImagePool;
  nsTArray<nsRefPtr<Image> > mImageQueue;
  bool mStop; 
  int mActiveImageCount;
};


} // namespace
} // namespace

#endif

