/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ImageContainerParent.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/SharedImageUtils.h"
#include "CompositorParent.h"

namespace mozilla {
namespace layers {

PGrallocBufferParent*
ImageContainerParent::AllocPGrallocBuffer(const gfxIntSize& aSize,
                                          const gfxContentType& aContent,
                                          MaybeMagicGrallocBufferHandle* aOutHandle)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  return GrallocBufferActor::Create(aSize, aContent, aOutHandle);
#else
  NS_RUNTIMEABORT("No gralloc buffers for you");
  return nsnull;
#endif
}

bool
ImageContainerParent::DeallocPGrallocBuffer(PGrallocBufferParent* actor)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  delete actor;
  return true;
#else
  NS_RUNTIMEABORT("Um, how did we get here?");
  return false;
#endif
}

bool ImageContainerParent::RecvPushSharedImage(const SharedImage& aImage)
{
  SharedImage * copy = new SharedImage(aImage);
  SharedImage * prevImage = SwapSharedImage(mID, copy);

  PRUint32 compositorID = GetCompositorIDForImage(mID);
  CompositorParent* compositor = CompositorParent::GetCompositor(compositorID);
  
  if (compositor) compositor->ScheduleComposition();
  
  if (prevImage) {
    SendReleasedSharedImage(*prevImage);
    delete prevImage;
  }
  return true;
}

bool ImageContainerParent::Recv__delete__()
{
  SharedImage* removed = RemoveSharedImage(mID);
  if (removed) {
    DeallocSharedImageData(this, *removed);
    delete removed;
  }

  return true;
}

ImageContainerParent::~ImageContainerParent()
{
  // On emergency shutdown, Recv__delete__ won't be invokked, so
  // we need to cleanup the global table here and not worry about
  // deallocating the shmem in the scenario since the emergency 
  // shutdown procedure takes care of that. 
  // On regular shutdown, Recv__delete__ also calls RemoveSharedImage
  // but it is not a problem because it is safe to call twice.
  RemoveSharedImage(mID);
}

namespace {
  // TODO: replace this naive map implementation with whatever  
  // everyone agrees on (std::map?)
  struct ImageIDPair {
    ImageIDPair(SharedImage* aImage, PRUint64 aID)
    : image(aImage), id(aID), version(1) {}
    SharedImage*  image;
    PRUint64      id;
    PRUint32      version;
    PRUint32      compositorID;
  };

  typedef nsTArray<ImageIDPair> SharedImageMap;
  SharedImageMap* sSharedImageMap = nsnull;

  enum {_INVALID_INDEX=-1};

  int IndexOf(PRUint64 aID)
  {
    for (int i = 0; i < sSharedImageMap->Length(); ++i) {
      if ((*sSharedImageMap)[i].id == aID) {
        return i;
      }
    }
    return _INVALID_INDEX;
  }
} // anonymous namespace

bool ImageContainerParent::IsExistingID(PRUint64 aID)
{
  return IndexOf(aID) != _INVALID_INDEX;
}

SharedImage* ImageContainerParent::SwapSharedImage(PRUint64 aID, 
                                                        SharedImage* aImage)
{
  int idx = IndexOf(aID);
  if (idx == _INVALID_INDEX) {
    sSharedImageMap->AppendElement(ImageIDPair(aImage,aID));
  return nsnull;
  }
  SharedImage* test = GetSharedImage(aID);
  SharedImage* prev = (*sSharedImageMap)[idx].image;
  (*sSharedImageMap)[idx].image = aImage;
  (*sSharedImageMap)[idx].version++;
  return prev;
}

PRUint32 ImageContainerParent::GetSharedImageVersion(PRUint64 aID)
{
  int idx = IndexOf(aID);
  if (idx == _INVALID_INDEX) return 0;
  return (*sSharedImageMap)[idx].version;
}

SharedImage* ImageContainerParent::RemoveSharedImage(PRUint64 aID) 
{
  int idx = IndexOf(aID);
  if (idx != _INVALID_INDEX) {
    SharedImage* img = (*sSharedImageMap)[idx].image;
    sSharedImageMap->RemoveElementAt(idx);
    return img;
  }
  return nsnull;
}

SharedImage* ImageContainerParent::GetSharedImage(PRUint64 aID)
{
  int idx = IndexOf(aID);
  if (idx != _INVALID_INDEX) {
    return (*sSharedImageMap)[idx].image;
  }
  return nsnull;
}

bool ImageContainerParent::SetCompositorIDForImage(PRUint64 aImageID, PRUint32 aCompositorID)
{
  int idx = IndexOf(aImageID);
  if (idx == _INVALID_INDEX) {
    return false;
  }
  (*sSharedImageMap)[idx].compositorID = aCompositorID;
  return true;
}

PRUint32 ImageContainerParent::GetCompositorIDForImage(PRUint64 aImageID)
{
  int idx = IndexOf(aImageID);
  if (idx != _INVALID_INDEX) {
    return (*sSharedImageMap)[idx].compositorID;
  }
  return 0;
}

void ImageContainerParent::CreateSharedImageMap()
{
  if (sSharedImageMap == nsnull) {
    sSharedImageMap = new SharedImageMap;
  }
}
void ImageContainerParent::DestroySharedImageMap()
{
  if (sSharedImageMap != nsnull) {
    delete sSharedImageMap;
    sSharedImageMap = nsnull;
  }
}

} // namespace
} // namespace
