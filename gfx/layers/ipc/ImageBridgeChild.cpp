/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageBridgeChild.h"
#include "ImageContainerChild.h"
#include "ImageBridgeParent.h"
#include "gfxSharedImageSurface.h"
#include "ImageLayers.h"
#include "base/thread.h"
#include "mozilla/ReentrantMonitor.h"

namespace mozilla {
namespace layers {

// Singleton 
namespace {
  ImageBridgeChild* sImageBridgeChildSingleton = nsnull;
}


namespace {
  void DestroyNow() 
  {
    NS_ABORT_IF_FALSE(sImageBridgeChildSingleton->InImageBridgeChildThread(),
                      "Should be in ImageBridgeChild thread.");
    if (sImageBridgeChildSingleton) {
      int numChildren = 
        sImageBridgeChildSingleton->ManagedPImageContainerChild().Length();
      for (int i = numChildren-1; i >= 0; --i) {
        static_cast<ImageContainerChild*>(
          sImageBridgeChildSingleton->ManagedPImageContainerChild()[i] 
        )->Destroy();
      }
    }
  }

  void DeleteNow()
  {
    NS_ABORT_IF_FALSE(sImageBridgeChildSingleton->InImageBridgeChildThread(),
                      "Should be in ImageBridgeChild thread.");
    delete sImageBridgeChildSingleton;
    sImageBridgeChildSingleton = nsnull;
  }
} // anponymous namespace




bool ImageBridgeChild::Create(base::Thread* aThread)
{
  if (sImageBridgeChildSingleton == nsnull) {
    sImageBridgeChildSingleton = new ImageBridgeChild(aThread);
    return true;
  } else {
    return false;
  }
}

class ImageBridgeDestroyTask : public Task
{
public:
  ImageBridgeDestroyTask(ReentrantMonitor* aBarrier) 
  : mBarrier(aBarrier) {}
  void Run() {
    ReentrantMonitorAutoEnter autoMon(*mBarrier);
    DestroyNow();
    mBarrier->NotifyAll();
  }
  ReentrantMonitor* mBarrier;
};

class ImageBridgeDeleteTask : public Task
{
public:
  ImageBridgeDeleteTask(ReentrantMonitor* aBarrier) 
  : mBarrier(aBarrier) {}
  void Run() {
    ReentrantMonitorAutoEnter autoMon(*mBarrier);
    DeleteNow();
    mBarrier->NotifyAll();
  }
  ReentrantMonitor* mBarrier;
};


void ImageBridgeChild::Destroy()
{
  if (!sImageBridgeChildSingleton) return;
  
  ReentrantMonitor barrier("ImageBridgeDestroyTask lock");
  Task * t1 = new ImageBridgeDestroyTask(&barrier);
  Task * t2 = new ImageBridgeDeleteTask(&barrier);
  ReentrantMonitorAutoEnter autoMon(barrier);
  sImageBridgeChildSingleton->GetMessageLoop()->PostTask(FROM_HERE, t1);
  barrier.Wait();
  sImageBridgeChildSingleton->GetMessageLoop()->PostTask(FROM_HERE, t2);
  barrier.Wait();
}

ImageBridgeChild* ImageBridgeChild::GetSingleton()
{
  return sImageBridgeChildSingleton;
}

bool ImageBridgeChild::IsCreated()
{
  return GetSingleton() != nsnull;
}

ImageBridgeChild::ImageBridgeChild(base::Thread* aThread)
{
  mThread = aThread;
  if (!mThread->IsRunning()) {
    mThread->Start();
  }
}

ImageBridgeChild::~ImageBridgeChild()
{
}

PImageContainerChild* ImageBridgeChild::AllocPImageContainer(PRUint64* id)
{
  NS_ABORT();
  return nsnull;
}

bool ImageBridgeChild::DeallocPImageContainer(PImageContainerChild* aImgContainerChild)
{
  delete aImgContainerChild;
  return true;
}



bool ImageBridgeChild::InImageBridgeChildThread() const
{
  return mThread->thread_id() == PlatformThread::CurrentId();
}



MessageLoop * ImageBridgeChild::GetMessageLoop() const
{
  return mThread->message_loop();
}


// async opertations:

class ImageBridgeConnectionTask : public Task
{
public:
  ImageBridgeConnectionTask(ImageBridgeChild * child, ImageBridgeParent * parent)
  : mChild(child), mParent(parent) {}

  void Run()
  {
    mChild->ConnectNow(mParent);
  }
private:
  ImageBridgeChild * mChild;
  ImageBridgeParent * mParent;
};

class ImageBridgeCreateContainerChildTask : public Task
{
public:
  ImageBridgeCreateContainerChildTask(ImageBridgeChild * child,
                                      ImageContainer* aContainer,
                                      ImageContainerChild** result, 
                                      ReentrantMonitor* barrier)
  : mChild(child), mContainer(aContainer), mResult(result), mSync(barrier) {}

  void Run()
  {
    ReentrantMonitorAutoEnter autoMon(*mSync);
    *mResult = mChild->CreateImageContainerChildNow(&(*mContainer));
    mSync->NotifyAll();
  }
private:
  ImageBridgeChild * mChild;
  nsRefPtr<ImageContainer> mContainer;
  ImageContainerChild** mResult;
  ReentrantMonitor* mSync;
};

class ImageBridgeAllocSurfaceDescriptorGrallocTask : public Task
{
public:
  ImageBridgeAllocSurfaceDescriptorGrallocTask(ImageBridgeChild * child,
                                               const gfxIntSize& aSize,
                                               const uint32_t& aFormat,
                                               SurfaceDescriptor* result,
                                               ReentrantMonitor* barrier)
    : mChild(child), mSize(aSize), mFormat(aFormat), mResult(result), mSync(barrier) {}

  void Run()
  {
    ReentrantMonitorAutoEnter autoMon(*mSync);
    mChild->AllocSurfaceDescriptorGrallocNow(mSize, mFormat, mResult);
    mSync->NotifyAll();
  }
private:
  ImageBridgeChild * mChild;
  gfxIntSize mSize;
  uint32_t mFormat;
  SurfaceDescriptor* mResult;
  ReentrantMonitor* mSync;
};

class ImageBridgeDeallocSurfaceDescriptorGrallocTask : public Task
{
public:
  ImageBridgeDeallocSurfaceDescriptorGrallocTask(ImageBridgeChild* child,
                                                 const SurfaceDescriptor& aBuffer,
                                                 ReentrantMonitor* barrier)
    : mChild(child), mBuffer(aBuffer), mSync(barrier) {}

  void Run()
  {
    ReentrantMonitorAutoEnter autoMon(*mSync);
    mChild->DeallocSurfaceDescriptorGrallocNow(mBuffer);
    mSync->NotifyAll();
  }
private:
  ImageBridgeChild * mChild;
  SurfaceDescriptor mBuffer;
  ReentrantMonitor* mSync;
};

void ImageBridgeChild::ConnectNow(ImageBridgeParent* aParent)
{
    MessageLoop * parentMsgLoop = aParent->GetMessageLoop();
    ipc::AsyncChannel * parentChannel = aParent->GetIPCChannel();
    this->Open(parentChannel, parentMsgLoop, mozilla::ipc::AsyncChannel::Child);
}

void ImageBridgeChild::ConnectAsync(ImageBridgeParent* aParent)
{
  Task * t = new ImageBridgeConnectionTask(this, aParent);
  GetMessageLoop()->PostTask(FROM_HERE, t);
}

PGrallocBufferChild*
ImageBridgeChild::AllocPGrallocBuffer(const gfxIntSize&,
                                      const uint32_t&,
                                      MaybeMagicGrallocBufferHandle*)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  return GrallocBufferActor::Create();
#else
  NS_RUNTIMEABORT("No gralloc buffers for you");
  return nsnull;
#endif
}

bool
ImageBridgeChild::DeallocPGrallocBuffer(PGrallocBufferChild* actor)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  delete actor;
  return true;
#else
  NS_RUNTIMEABORT("Um, how did we get here?");
  return false;
#endif
}

bool
ImageBridgeChild::AllocSurfaceDescriptorGralloc(const gfxIntSize& aSize,
                                                const uint32_t& aFormat,
                                                SurfaceDescriptor* aBuffer)
{
  if (InImageBridgeChildThread()) {
    return ImageBridgeChild::AllocSurfaceDescriptorGrallocNow(aSize, aFormat, aBuffer);
  }

  ReentrantMonitor barrier("CreateSurfaceDescriptor Lock");
  ReentrantMonitorAutoEnter autoMon(barrier);

  Task* t = new ImageBridgeAllocSurfaceDescriptorGrallocTask(this, aSize, aFormat, aBuffer, &barrier);
  GetMessageLoop()->PostTask(FROM_HERE, t);

  barrier.Wait();
  return true;
}

bool
ImageBridgeChild::AllocSurfaceDescriptorGrallocNow(const gfxIntSize& aSize,
                                                   const uint32_t& aFormat,
                                                   SurfaceDescriptor* aBuffer)
{
  MaybeMagicGrallocBufferHandle handle;
  PGrallocBufferChild* gc = SendPGrallocBufferConstructor(aSize, aFormat, &handle);
  if (handle.Tnull_t == handle.type()) {
    PGrallocBufferChild::Send__delete__(gc);
    return false;
  }

  GrallocBufferActor* gba = static_cast<GrallocBufferActor*>(gc);
  gba->InitFromHandle(handle.get_MagicGrallocBufferHandle());

  *aBuffer = SurfaceDescriptorGralloc(nsnull, gc);
  return true;
}

bool
ImageBridgeChild::DeallocSurfaceDescriptorGralloc(const SurfaceDescriptor& aBuffer)
{
  if (InImageBridgeChildThread()) {
    return ImageBridgeChild::DeallocSurfaceDescriptorGrallocNow(aBuffer);
  }

  ReentrantMonitor barrier("DeallocSurfaceDescriptor Lock");
  ReentrantMonitorAutoEnter autoMon(barrier);

  Task* t = new ImageBridgeDeallocSurfaceDescriptorGrallocTask(this, aBuffer, &barrier);
  GetMessageLoop()->PostTask(FROM_HERE, t);

  barrier.Wait();

  return true;
}

bool
ImageBridgeChild::DeallocSurfaceDescriptorGrallocNow(const SurfaceDescriptor& aBuffer)
{
  PGrallocBufferChild* gbp =
    aBuffer.get_SurfaceDescriptorGralloc().bufferChild();
  PGrallocBufferChild::Send__delete__(gbp);

  return true;
}

ImageContainerChild* ImageBridgeChild::CreateImageContainerChild(ImageContainer* aContainer)
{
  if (InImageBridgeChildThread()) {
    return ImageBridgeChild::CreateImageContainerChildNow(aContainer); 
  } 
 
  // ImageContainerChild can only be alocated on the ImageBridgeChild thread, so se
  // dispatch a task to the thread and block the current thread until the task has been
  // executed.
  ImageContainerChild* result = nsnull;
  
  ReentrantMonitor barrier("CreateImageContainerChild Lock");
  ReentrantMonitorAutoEnter autoMon(barrier);

  Task * t = new ImageBridgeCreateContainerChildTask(this, aContainer, &result, &barrier);
  GetMessageLoop()->PostTask(FROM_HERE, t);

  // should stop the thread until the ImageContainerChild has been created on 
  // the other thread
  barrier.Wait();
  return result;
}

ImageContainerChild* ImageBridgeChild::CreateImageContainerChildNow(ImageContainer* aContainer)
{
  ImageContainerChild* ctnChild = new ImageContainerChild(this,aContainer);
  PRUint64 id = 0; 
  SendPImageContainerConstructor(ctnChild, &id);
  ctnChild->SetImageID(id);
  aContainer->SetImageContainerChild(ctnChild);
  return ctnChild;
}

} // layers
} // mozilla
