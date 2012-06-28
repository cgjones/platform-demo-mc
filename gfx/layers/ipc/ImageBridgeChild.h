/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGEBRIDGECHILD_H
#define MOZILLA_GFX_IMAGEBRIDGECHILD_H

#include "mozilla/layers/PImageBridgeChild.h"

class gfxSharedImageSurface;

namespace base {
class Thread;
}

namespace mozilla {
namespace layers {

class ImageContainerChild;
class ImageBridgeParent;
class ImageBridgeDestroyTask;
class ImageBridgeConnectionTask;
class ImageBridgeCreateContainerChildTask;
class SharedImage;
class Image;

class ImageBridgeChild : public PImageBridgeChild
{
friend class mozilla::layers::ImageBridgeDestroyTask;
friend class mozilla::layers::ImageBridgeConnectionTask;
friend class mozilla::layers::ImageBridgeCreateContainerChildTask;

public:

  /**
   * Creates the ImageBridgeChild manager protocol.
   */
  static bool Create(base::Thread* aThread);

  /**
   * Destroys The ImageBridge protcol.
   *
   * The actual destruction happens synchronously on the ImageBridgeChild thread
   * which means that if this function is called from another thread, the current 
   * thread will be paused until the destruction is done.
   */
  static void Destroy();
  
  /**
   * Returns true if the singleton has been created.
   *
   * Can be called from any thread.
   */
  static bool IsCreated();

  /**
   * returns the singleton instance.
   *
   * can be called from any thread.
   */
  static ImageBridgeChild* GetSingleton();


  /**
   * Dispatches a task to the ImageBridgeChild thread 
   */
  void ConnectAsync(ImageBridgeParent* aParent);
    
  /**
   * Returns the ImageBrdugeChild's thread.
   *
   * Can be called from any thread.
   */
  base::Thread * GetThread() const
  {
    return mThread;
  }

  MessageLoop * GetMessageLoop() const;

  /**
   * Returns true if the current thread is the ImageBrdugeChild's thread.
   *
   * Can be called from any thread.
   */
  bool InImageBridgeChildThread() const;

  /**
   * Creates an ImageContainerChild and it's associated ImageContainerParent.
   *
   * The creation happens synchronously on the ImageBridgeChild thread, so if 
   * this function is called on another thread, the current thread will be 
   * paused until the creation is done.
   */
  ImageContainerChild* CreateImageContainerChild(ImageContainer* aContainer);

  virtual PGrallocBufferChild*
  AllocPGrallocBuffer(const gfxIntSize&, const uint32_t&,
                      MaybeMagicGrallocBufferHandle*) MOZ_OVERRIDE;

  virtual bool
  DeallocPGrallocBuffer(PGrallocBufferChild* actor) MOZ_OVERRIDE;

  bool
  AllocSurfaceDescriptorGralloc(const gfxIntSize& aSize,
                                const uint32_t& aContent,
                                SurfaceDescriptor* aBuffer);

  bool
  DeallocSurfaceDescriptorGralloc(const SurfaceDescriptor& aBuffer);

  // overriden from PImageBridgeChild
  PImageContainerChild* AllocPImageContainer(PRUint64*);
  // overriden from PImageBridgeChild
  bool DeallocPImageContainer(PImageContainerChild* aImgContainerChild);

  ImageBridgeChild(base::Thread* aThread);
  ~ImageBridgeChild();

protected:

  /**
   * Part of the connection that is executed on the ImageBridgeChild thread
   * after invoking Connect.
   *
   * Must be called from the ImageBridgeChild thread.
   */
  void ConnectNow(ImageBridgeParent* aParent);

  /**
   * Part of the creation of ImageCOntainerCHild that is executed on the 
   * ImageBridgeCild thread after invoking CreateImageContainerChild
   *
   * Must be called from the ImageBridgeChild thread.
   */
  ImageContainerChild* CreateImageContainerChildNow(ImageContainer* aContainer);
  
private:
  base::Thread * mThread;
};

} // layers
} // mozilla


#endif

