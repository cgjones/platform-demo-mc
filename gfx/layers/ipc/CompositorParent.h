/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_CompositorParent_h
#define mozilla_layers_CompositorParent_h

// Enable this pref to turn on compositor performance warning.
// This will print warnings if the compositor isn't meeting
// its responsiveness objectives:
//    1) Compose a frame within 15ms of receiving a ScheduleCompositeCall
//    2) Unless a frame was composited within the throttle threshold in
//       which the deadline will be 15ms + throttle threshold
//#define COMPOSITOR_PERFORMANCE_WARNING

#include "mozilla/layers/PCompositorParent.h"
#include "mozilla/layers/PLayersParent.h"
#include "base/thread.h"
#include "mozilla/Monitor.h"
#include "ShadowLayersManager.h"

class nsIWidget;

namespace base {
class Thread;
}

namespace mozilla {
namespace layers {

class LayerManager;
class AsyncPanZoomController;

// Represents (affine) transforms that are calculated from a content view.
struct ViewTransform {
  ViewTransform(nsIntPoint aTranslation = nsIntPoint(0, 0), float aXScale = 1, float aYScale = 1)
    : mTranslation(aTranslation)
    , mXScale(aXScale)
    , mYScale(aYScale)
  {}

  operator gfx3DMatrix() const
  {
    return
      gfx3DMatrix::ScalingMatrix(mXScale, mYScale, 1) *
      gfx3DMatrix::Translation(mTranslation.x, mTranslation.y, 0);
  }

  nsIntPoint mTranslation;
  float mXScale;
  float mYScale;
};

class CompositorParent : public PCompositorParent,
                         public ShadowLayersManager
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositorParent)
public:
  CompositorParent(nsIWidget* aWidget,
                   bool aRenderToEGLSurface = false,
                   int aSurfaceWidth = -1, int aSurfaceHeight = -1);

  virtual ~CompositorParent();

  virtual bool RecvWillStop() MOZ_OVERRIDE;
  virtual bool RecvStop() MOZ_OVERRIDE;
  virtual bool RecvPause() MOZ_OVERRIDE;
  virtual bool RecvResume() MOZ_OVERRIDE;

  virtual void ShadowLayersUpdated(ShadowLayersParent* aLayerTree,
                                   bool isFirstPaint) MOZ_OVERRIDE;
  void Destroy();

  LayerManager* GetLayerManager() { return mLayerManager; }

  void SetTransformation(float aScale, nsIntPoint aScrollOffset);
  void AsyncRender();

  // Can be called from any thread
  void ScheduleRenderOnCompositorThread();
  void SchedulePauseOnCompositorThread();
  void ScheduleResumeOnCompositorThread(int width, int height);

  virtual void ScheduleComposition();

  void SetAsyncPanZoomController(AsyncPanZoomController* aAsyncPanZoomController);

  /** Runs on the "main thread". */
  static int64_t AllocateLayerTreeId();

  /** */
  static PCompositorParent*
  Create(Transport* aTransport, ProcessId aOtherProcess);

  /**
   * Creates a global map referencing each compositor by ID.
   *
   * This map is used by the ImageBirdge protocol to trigger
   * compositions without having to keep references to the 
   * compositor
   */
  static void CreateCompositorMap();
  static void DestroyCompositorMap();
  /**
   * Returns a pointer to the compositor corrspoinding to the given ID. 
   */
  static CompositorParent* GetCompositor(PRUint32 id);
  
  /**
   * Creates the compositor thread.
   *
   * All compositors live on the same thread.
   * The thread is not lazily created on first access to avoid dealing with 
   * thread safety. Therefore it's best to create and destroy the thread when
   * we know we areb't using it (So creating/destroying along with gfxPlatform 
   * looks like a good place).
   */
  static bool CreateThread();

  /**
   * Destroys the compositor thread.
   *
   * It is safe to call this fucntion more than once, although the second call
   * will have no effect.
   * This function is not thread-safe.
   */
  static void DestroyThread();

  /**
   * Returns the compositor thread's message loop.
   *
   * This message loop is used by CompositorParent and ImageBridgeParent.
   */
  static MessageLoop* CompositorLoop();

protected:
  virtual PLayersParent* AllocPLayers(const LayersBackend& aBackendType, const int64_t& aId, int32_t* aMaxTextureSize);
  virtual bool DeallocPLayers(PLayersParent* aLayers);
  virtual void ScheduleTask(CancelableTask*, int);
  virtual void Composite();
  virtual void SetFirstPaintViewport(const nsIntPoint& aOffset, float aZoom, const nsIntRect& aPageRect, const gfx::Rect& aCssPageRect);
  virtual void SetPageRect(const gfx::Rect& aCssPageRect);
  virtual void SyncViewportInfo(const nsIntRect& aDisplayPort, float aDisplayResolution, bool aLayersUpdated,
                                nsIntPoint& aScrollOffset, float& aScaleX, float& aScaleY);
  void SetEGLSurfaceSize(int width, int height);

private:
  CompositorParent();

  void PauseComposition();
  void ResumeComposition();
  void ResumeCompositionAndResize(int width, int height);

  void TransformShadowTree();

  /**
   * Add a compositor to the global compositor map.
   */
  static PRUint32 AddCompositor(CompositorParent* compositor);
  /**
   * Remove a compositor from the global compositor map.
   */
  static CompositorParent* RemoveCompositor(PRUint32 id);


  inline PlatformThreadId CompositorThreadID();

  // Platform specific functions
  /**
   * Does a breadth-first search to find the first layer in the tree with a
   * displayport set.
   */
  Layer* GetPrimaryScrollableLayer();

  /**
   * Recursively applies the given translation to all fixed position layers
   * that aren't children of other fixed position layers.
   */
  void TranslateFixedLayers(Layer* aLayer, const gfxPoint& aTranslation);

  //
  nsRefPtr<CompositorParent> mSelfRef;
  nsRefPtr<LayerManager> mLayerManager;
  nsIWidget* mWidget;
  CancelableTask *mCurrentCompositeTask;
  TimeStamp mLastCompose;
#ifdef COMPOSITOR_PERFORMANCE_WARNING
  TimeStamp mExpectedComposeTime;
#endif

  bool mPaused;
  float mXScale;
  float mYScale;
  nsIntPoint mScrollOffset;
  nsIntRect mContentRect;
  nsIntSize mWidgetSize;

  nsRefPtr<AsyncPanZoomController> mAsyncPanZoomController;

  // When this flag is set, the next composition will be the first for a
  // particular document (i.e. the document displayed on the screen will change).
  // This happens when loading a new page or switching tabs. We notify the
  // front-end (e.g. Java on Android) about this so that it take the new page
  // size and zoom into account when providing us with the next view transform.
  bool mIsFirstPaint;

  // This flag is set during a layers update, so that the first composition
  // after a layers update has it set. It is cleared after that first composition.
  bool mLayersUpdated;

  bool mRenderToEGLSurface;
  nsIntSize mEGLSurfaceSize;

  mozilla::Monitor mPauseCompositionMonitor;
  mozilla::Monitor mResumeCompositionMonitor;

  PRUint32 mCompositorID;

  DISALLOW_EVIL_CONSTRUCTORS(CompositorParent);

  friend class AsyncPanZoomController;
};

} // layers
} // mozilla

#endif // mozilla_layers_CompositorParent_h
