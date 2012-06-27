/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "ImageContainerChild.h"
#include "gfxPlatform.h"
#include "gfxSharedImageSurface.h"
#include "ShadowLayers.h"
#include "mozilla/layers/PLayers.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/layers/SharedImageUtils.h"


namespace mozilla {
namespace layers {

enum {POOL_MAX_SHARED_IMAGES = 10, MAX_ACTIVE_SHARED_IMAGES=10};


ImageContainerChild::~ImageContainerChild()
{
}

bool ImageContainerChild::RecvReleasedSharedImage(const SharedImage& aImage)
{
  SharedImage* img = new SharedImage(aImage);
  if (!AddSharedImageToPool(img) || mStop) {
    DeallocSharedImageData(this, *img);
    delete img;
  }
  return true;
}

bool ImageContainerChild::AllocBuffer(const gfxIntSize& aSize,
                                   gfxASurface::gfxContentType aContent,
                                   gfxSharedImageSurface** aBuffer)
{
  SharedMemory::SharedMemoryType shmemType = ipc::OptimalShmemType();
  gfxASurface::gfxImageFormat format = gfxPlatform::GetPlatform()->OptimalFormatForContent(aContent);

  nsRefPtr<gfxSharedImageSurface> back =
    gfxSharedImageSurface::CreateUnsafe(this, aSize, format, shmemType);
  if (!back)
    return false;

  *aBuffer = nsnull;
  back.swap(*aBuffer);
  return true;
}

void ImageContainerChild::DestroySharedImage(const SharedImage& aImage)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");

  --mActiveImageCount;
  DeallocSharedImageData(this, aImage);
}


bool ImageContainerChild::CopyDataIntoSharedImage(Image* src, SharedImage* dest)
{
  if ((src->GetFormat() == Image::PLANAR_YCBCR) && 
      (dest->type() == SharedImage::TYUVImage)) {
    PlanarYCbCrImage *YCbCrImage = static_cast<PlanarYCbCrImage*>(src);
    const PlanarYCbCrImage::Data *data = YCbCrImage->GetData();
    NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
    YUVImage& yuv = dest->get_YUVImage();

    nsRefPtr<gfxSharedImageSurface> surfY =
      gfxSharedImageSurface::Open(yuv.Ydata());
    nsRefPtr<gfxSharedImageSurface> surfU =
      gfxSharedImageSurface::Open(yuv.Udata());
    nsRefPtr<gfxSharedImageSurface> surfV =
      gfxSharedImageSurface::Open(yuv.Vdata());

    gfxIntSize size = surfY->GetSize();
    //gfxIntSize CbCrSize = surfU->GetSize();
  
    NS_ABORT_IF_FALSE(size == mSize, "Sizes must match to copy image data.");

    for (int i = 0; i < data->mYSize.height; i++) {
      memcpy(surfY->Data() + i * surfY->Stride(),
             data->mYChannel + i * data->mYStride,
             data->mYSize.width);
    }
    for (int i = 0; i < data->mCbCrSize.height; i++) {
      memcpy(surfU->Data() + i * surfU->Stride(),
             data->mCbChannel + i * data->mCbCrStride,
             data->mCbCrSize.width);
      memcpy(surfV->Data() + i * surfV->Stride(),
             data->mCrChannel + i * data->mCbCrStride,
             data->mCbCrSize.width);
    }

    return true;
  }
  return false; // TODO: support more image formats
}

SharedImage* ImageContainerChild::CreateSharedImageFromData(Image* image)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                  "Should be in ImageBridgeChild thread.");
  
  ++mActiveImageCount;

  // TODO: I don't test for BasicManager()->IsCompositingCheap() here,
  // is this a problem? (the equvivalent code in PCompositor does that)
  if (image->GetFormat() == Image::PLANAR_YCBCR ) {
    PlanarYCbCrImage *YCbCrImage = static_cast<PlanarYCbCrImage*>(image);
    const PlanarYCbCrImage::Data *data = YCbCrImage->GetData();
    NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
    
    nsRefPtr<gfxSharedImageSurface> tempBufferY;
    nsRefPtr<gfxSharedImageSurface> tempBufferU;
    nsRefPtr<gfxSharedImageSurface> tempBufferV;
    
    if (!this->AllocBuffer(data->mYSize, gfxASurface::CONTENT_ALPHA,
                           getter_AddRefs(tempBufferY)) ||
        !this->AllocBuffer(data->mCbCrSize, gfxASurface::CONTENT_ALPHA,
                           getter_AddRefs(tempBufferU)) ||
        !this->AllocBuffer(data->mCbCrSize, gfxASurface::CONTENT_ALPHA,
                           getter_AddRefs(tempBufferV))) {
      NS_RUNTIMEABORT("creating SharedImage failed!");
    }

    for (int i = 0; i < data->mYSize.height; i++) {
      memcpy(tempBufferY->Data() + i * tempBufferY->Stride(),
             data->mYChannel + i * data->mYStride,
             data->mYSize.width);
    }
    for (int i = 0; i < data->mCbCrSize.height; i++) {
      memcpy(tempBufferU->Data() + i * tempBufferU->Stride(),
             data->mCbChannel + i * data->mCbCrStride,
             data->mCbCrSize.width);
      memcpy(tempBufferV->Data() + i * tempBufferV->Stride(),
             data->mCrChannel + i * data->mCbCrStride,
             data->mCbCrSize.width);
    }

    SharedImage* result = new SharedImage( 
              *(new YUVImage(tempBufferY->GetShmem(),
                                             tempBufferU->GetShmem(),
                                             tempBufferV->GetShmem(),
                                             data->GetPictureRect())));
    NS_ABORT_IF_FALSE(result->type() == SharedImage::TYUVImage,
                      "SharedImage type not set correctly");
    return result;
  } else {
    NS_RUNTIMEABORT("TODO: Only YUVImage is supported here right now.");
  }
  return nsnull;
}

bool ImageContainerChild::AddSharedImageToPool(SharedImage* img)
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(), 
                    "AddSharedImageToPool must be called in the ImageBridgeChild thread");
  if (mStop) return false;

  if (mSharedImagePool.Length() >= POOL_MAX_SHARED_IMAGES) {
    return false;
  }
  if (img->type()==SharedImage::TYUVImage) {
    nsIntRect rect = img->get_YUVImage().picture();
    if ((rect.Width() != mSize.width) || (rect.Height() != mSize.height)) {
      ClearSharedImagePool();
      mSize.width = rect.Width();
      mSize.height = rect.Height();
    }
    mSharedImagePool.AppendElement(img);
    return true;
  }
  return false; // TODO accept more image formats in the pool
}

SharedImage* ImageContainerChild::PopSharedImageFromPool()
{
  if (mSharedImagePool.Length() > 0) {
    SharedImage* img = mSharedImagePool[mSharedImagePool.Length()-1];
    mSharedImagePool.RemoveElement(mSharedImagePool.LastElement());
    return img;
  }
  
  return nsnull;
}

void ImageContainerChild::ClearSharedImagePool()
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");
  for(unsigned int i = 0; i < mSharedImagePool.Length(); ++i) {
    DeallocSharedImageData(this, *mSharedImagePool[i]);
  }
  mSharedImagePool.Clear();
}

// TODO! handle ref count more gracefully
class ImageBridgeCopyAndSendTask : public Task
{
public:
  ImageBridgeCopyAndSendTask(ImageContainerChild * child, 
                             ImageContainer * aContainer, 
                             Image * aImage)
  : mChild(child), mImageContainer(aContainer), mImage(aImage) {}

  void Run()
  { 
    SharedImage* img = mChild->ImageToSharedImage(&(*mImage));
    if (img) {
      mChild->SendPushSharedImage(*img);
    }
  }
private:
  ImageContainerChild * mChild;
  nsRefPtr<ImageContainer> mImageContainer;
  nsRefPtr<Image> mImage;
};

SharedImage* ImageContainerChild::ImageToSharedImage(Image* aImage)
{
  if (mStop) return nsnull;
  if (mActiveImageCount > MAX_ACTIVE_SHARED_IMAGES) {
    // Too many active shared images, perhaps the compositor is hanging.
    // Skipping current image
    return nsnull;
  }

  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");
  SharedImage* img = PopSharedImageFromPool();
    if (img) {
      CopyDataIntoSharedImage(aImage, img);  
    } else {
      img = CreateSharedImageFromData(aImage);
    }
  return img;
}

void ImageContainerChild::SendImageAsync(ImageContainer* aContainer,
                                         Image* aImage)
{
  if(!aContainer || !aImage) {
      return;
  }

  if (mStop) return;

  if (InImageBridgeChildThread()) {
    SharedImage* img = ImageToSharedImage(aImage);
    if (img) SendPushSharedImage(*img);
    delete img;
    return;
  }

  // Sending images and (potentially) allocating shmems must be done 
  // on the ImageBrdgeChild thread.
  Task * t = new ImageBridgeCopyAndSendTask(this, aContainer, aImage);   
  GetMessageLoop()->PostTask(FROM_HERE, t);
}



class ImageContainerChildDestroyTask : public Task
{
public:
  ImageContainerChildDestroyTask(ImageContainerChild* aChild)
  : mImageContainerChild(aChild) {}
  void Run() 
  {
    mImageContainerChild->DestroyNow();
  }
private:
  ImageContainerChild* mImageContainerChild;
};

void ImageContainerChild::DestroyNow()
{
  NS_ABORT_IF_FALSE(InImageBridgeChildThread(),
                    "Should be in ImageBridgeChild thread.");

  ClearSharedImagePool();

  Send__delete__(this);
}

void ImageContainerChild::Destroy()
{
  if (mStop) return;

  mStop = true;

  if (mImageContainer) {
    mImageContainer->SetImageContainerChild(nsnull);
  }
  if (InImageBridgeChildThread()) {
    DestroyNow();
    return;
  }

  // destruction must be done on the ImageBridgeChild thread
  Task * t = new ImageContainerChildDestroyTask(this);   
  GetMessageLoop()->PostTask(FROM_HERE, t);  
}

} // namespace
} // namespace

