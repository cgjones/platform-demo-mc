/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <map>

#include "base/basictypes.h"
#include "base/message_loop.h"
#include "base/thread.h"

#include "CompositorParent.h"
#include "AsyncPanZoomController.h"
#include "RenderTrace.h"
#include "ShadowLayersParent.h"
#include "BasicLayers.h"
#include "LayerManagerOGL.h"
#include "nsIWidget.h"
#include "nsGkAtoms.h"
#include "RenderTrace.h"
#include "nsStyleAnimation.h"
#include "nsDisplayList.h"
#include "AnimationCommon.h"
#include "nsAnimationManager.h"
#include "mozilla/Preferences.h"

#if defined(MOZ_WIDGET_ANDROID)
#include "AndroidBridge.h"
#include <android/log.h>
#endif

using namespace base;
using namespace mozilla::ipc;
using namespace std;

namespace mozilla {
namespace layers {

static CompositorParent* sCurrent;
static base::Thread* sCompositorThread = nsnull;

static Layer* GetIndirectShadowTree(int64_t aId);

bool CompositorParent::CreateThread()
{
  if (sCompositorThread) {
    return true;
  }
  sCompositorThread = new base::Thread("Compositor");
  if (!sCompositorThread->Start()) {
    delete sCompositorThread;
    sCompositorThread = nsnull;
    return false;
  }
  return true;
}

void CompositorParent::DestroyThread()
{
  if (sCompositorThread) {
    delete sCompositorThread;
    sCompositorThread = nsnull;
  }
}

MessageLoop* CompositorParent::CompositorLoop()
{
  if (sCompositorThread) {
    return sCompositorThread->message_loop();
  }
  return nsnull;
}

CompositorParent::CompositorParent(nsIWidget* aWidget,
                                   bool aRenderToEGLSurface,
                                   int aSurfaceWidth, int aSurfaceHeight)
  : mWidget(aWidget)
  , mCurrentCompositeTask(NULL)
  , mPaused(false)
  , mXScale(1.0)
  , mYScale(1.0)
  , mIsFirstPaint(false)
  , mLayersUpdated(false)
  , mRenderToEGLSurface(aRenderToEGLSurface)
  , mEGLSurfaceSize(aSurfaceWidth, aSurfaceHeight)
  , mPauseCompositionMonitor("PauseCompositionMonitor")
  , mResumeCompositionMonitor("ResumeCompositionMonitor")
{
  NS_ABORT_IF_FALSE(sCompositorThread != nsnull, 
                    "The compositor thread must be Initialized before instanciating a COmpositorParent.");
  MOZ_COUNT_CTOR(CompositorParent);
  mCompositorID = AddCompositor(this);

  sCurrent = this;
}

CompositorParent::~CompositorParent()
{
  MOZ_COUNT_DTOR(CompositorParent);

  if (sCurrent == this)
    sCurrent = nsnull;
}

void
CompositorParent::Destroy()
{
  NS_ABORT_IF_FALSE(ManagedPLayersParent().Length() == 0,
                    "CompositorParent destroyed before managed PLayersParent");

  // Ensure that the layer manager is destructed on the compositor thread.
  mLayerManager = NULL;
}

bool
CompositorParent::RecvWillStop()
{
  mPaused = true;
  RemoveCompositor(mCompositorID);
  // Ensure that the layer manager is destroyed before CompositorChild.
  mLayerManager->Destroy();

  return true;
}

bool
CompositorParent::RecvStop()
{
  Destroy();
  return true;
}

bool
CompositorParent::RecvPause()
{
  PauseComposition();
  return true;
}

bool
CompositorParent::RecvResume()
{
  ResumeComposition();
  return true;
}

void
CompositorParent::ScheduleRenderOnCompositorThread()
{
  CancelableTask *renderTask = NewRunnableMethod(this, &CompositorParent::ScheduleComposition);
  CompositorLoop()->PostTask(FROM_HERE, renderTask);
}

void
CompositorParent::PauseComposition()
{
  //NS_ABORT_IF_FALSE(CompositorThreadID() == PlatformThread::CurrentId(),
  //                  "PauseComposition() can only be called on the compositor thread");

  MonitorAutoLock lock(mPauseCompositionMonitor);

  if (!mPaused) {
    mPaused = true;

#ifdef MOZ_WIDGET_ANDROID
    static_cast<LayerManagerOGL*>(mLayerManager.get())->gl()->ReleaseSurface();
#endif
  }

  // if anyone's waiting to make sure that composition really got paused, tell them
  lock.NotifyAll();
}

void
CompositorParent::ResumeComposition()
{
  //NS_ABORT_IF_FALSE(CompositorThreadID() == PlatformThread::CurrentId(),
  //                  "ResumeComposition() can only be called on the compositor thread");

  MonitorAutoLock lock(mResumeCompositionMonitor);

  mPaused = false;

#ifdef MOZ_WIDGET_ANDROID
  static_cast<LayerManagerOGL*>(mLayerManager.get())->gl()->RenewSurface();
#endif

  Composite();

  // if anyone's waiting to make sure that composition really got resumed, tell them
  lock.NotifyAll();
}

void
CompositorParent::SetEGLSurfaceSize(int width, int height)
{
  NS_ASSERTION(mRenderToEGLSurface, "Compositor created without RenderToEGLSurface ar provided");
  mEGLSurfaceSize.SizeTo(width, height);
  if (mLayerManager) {
    static_cast<LayerManagerOGL*>(mLayerManager.get())->SetSurfaceSize(mEGLSurfaceSize.width, mEGLSurfaceSize.height);
  }
}

void
CompositorParent::ResumeCompositionAndResize(int width, int height)
{
  mWidgetSize.width = width;
  mWidgetSize.height = height;
  SetEGLSurfaceSize(width, height);
  ResumeComposition();
}

/*
 * This will execute a pause synchronously, waiting to make sure that the compositor
 * really is paused.
 */
void
CompositorParent::SchedulePauseOnCompositorThread()
{
  MonitorAutoLock lock(mPauseCompositionMonitor);

  CancelableTask *pauseTask = NewRunnableMethod(this,
                                                &CompositorParent::PauseComposition);
  CompositorLoop()->PostTask(FROM_HERE, pauseTask);

  // Wait until the pause has actually been processed by the compositor thread
  lock.Wait();
}

void
CompositorParent::ScheduleResumeOnCompositorThread(int width, int height)
{
  MonitorAutoLock lock(mResumeCompositionMonitor);

  CancelableTask *resumeTask =
    NewRunnableMethod(this, &CompositorParent::ResumeCompositionAndResize, width, height);
  CompositorLoop()->PostTask(FROM_HERE, resumeTask);

  // Wait until the resume has actually been processed by the compositor thread
  lock.Wait();
}

void
CompositorParent::ScheduleTask(CancelableTask* task, int time)
{
  if (time == 0) {
    MessageLoop::current()->PostTask(FROM_HERE, task);
  } else {
    MessageLoop::current()->PostDelayedTask(FROM_HERE, task, time);
  }
}

void
CompositorParent::ScheduleComposition()
{
  if (mCurrentCompositeTask) {
    return;
  }

  bool initialComposition = mLastCompose.IsNull();
  TimeDuration delta;
  if (!initialComposition)
    delta = mozilla::TimeStamp::Now() - mLastCompose;

#ifdef COMPOSITOR_PERFORMANCE_WARNING
  mExpectedComposeTime = mozilla::TimeStamp::Now() + TimeDuration::FromMilliseconds(15);
#endif

  mCurrentCompositeTask = NewRunnableMethod(this, &CompositorParent::Composite);

  // Since 60 fps is the maximum frame rate we can acheive, scheduling composition
  // events less than 15 ms apart wastes computation..
  if (!initialComposition && delta.ToMilliseconds() < 15) {
#ifdef COMPOSITOR_PERFORMANCE_WARNING
    mExpectedComposeTime = mozilla::TimeStamp::Now() + TimeDuration::FromMilliseconds(15 - delta.ToMilliseconds());
#endif
    ScheduleTask(mCurrentCompositeTask, 15 - delta.ToMilliseconds());
  } else {
    ScheduleTask(mCurrentCompositeTask, 0);
  }
}

void
CompositorParent::SetTransformation(float aScale, nsIntPoint aScrollOffset)
{
  mXScale = aScale;
  mYScale = aScale;
  mScrollOffset = aScrollOffset;
}

class NS_STACK_CLASS AutoResolveRefLayers {
public:
  AutoResolveRefLayers(Layer* aRoot) : mRoot(aRoot)
  { WalkTheTree<Resolve>(mRoot, nsnull); }

  ~AutoResolveRefLayers()
  { WalkTheTree<Clear>(mRoot, nsnull); }

private:
  enum Op { Resolve, Clear };
  template<Op OP>
  void WalkTheTree(Layer* aLayer, Layer* aParent)
  {
    if (RefLayer* ref = aLayer->AsRefLayer()) {
      if (Layer* referent = GetIndirectShadowTree(ref->GetReferentId())) {
        if (OP == Resolve) {
          ref->ConnectReferentLayer(referent);
        } else {
          ref->ClearReferentLayer(referent);
        }
      }
    }
    for (Layer* child = aLayer->GetFirstChild();
         child; child = child->GetNextSibling()) {
      WalkTheTree<OP>(child, aLayer);
    }
  }

  Layer* mRoot;

  AutoResolveRefLayers(const AutoResolveRefLayers&);
  AutoResolveRefLayers& operator=(const AutoResolveRefLayers&);
};

void
CompositorParent::Composite()
{
  //NS_ABORT_IF_FALSE(CompositorThreadID() == PlatformThread::CurrentId(),
  //                  "Composite can only be called on the compositor thread");
  mCurrentCompositeTask = NULL;

  mLastCompose = mozilla::TimeStamp::Now();

  if (mPaused || !mLayerManager || !mLayerManager->GetRoot()) {
    return;
  }

  Layer* layer = mLayerManager->GetRoot();
  AutoResolveRefLayers resolve(layer);

  TransformShadowTree();

  mozilla::layers::RenderTraceLayers(layer, "0000");
  mLayerManager->EndEmptyTransaction();

#ifdef COMPOSITOR_PERFORMANCE_WARNING
  if (mExpectedComposeTime + TimeDuration::FromMilliseconds(15) < mozilla::TimeStamp::Now()) {
    printf_stderr("Compositor: Composite took %i ms.\n",
                  15 + (int)(mozilla::TimeStamp::Now() - mExpectedComposeTime).ToMilliseconds());
  }
#endif
}

// Do a breadth-first search to find the first layer in the tree that is
// scrollable.
Layer*
CompositorParent::GetPrimaryScrollableLayer()
{
  Layer* root = mLayerManager->GetRoot();

  nsTArray<Layer*> queue;
  queue.AppendElement(root);
  while (queue.Length()) {
    ContainerLayer* containerLayer = queue[0]->AsContainerLayer();
    queue.RemoveElementAt(0);
    if (!containerLayer) {
      continue;
    }

    const FrameMetrics& frameMetrics = containerLayer->GetFrameMetrics();
    if (frameMetrics.IsScrollable()) {
      return containerLayer;
    }

    Layer* child = containerLayer->GetFirstChild();
    while (child) {
      queue.AppendElement(child);
      child = child->GetNextSibling();
    }
  }

  return root;
}

static void
Translate2D(gfx3DMatrix& aTransform, const gfxPoint& aOffset)
{
  aTransform._41 += aOffset.x;
  aTransform._42 += aOffset.y;
}

void
CompositorParent::TranslateFixedLayers(Layer* aLayer,
                                       const gfxPoint& aTranslation)
{
  if (aLayer->GetIsFixedPosition() &&
      !aLayer->GetParent()->GetIsFixedPosition()) {
    gfx3DMatrix layerTransform = aLayer->GetTransform();
    Translate2D(layerTransform, aTranslation);
    ShadowLayer* shadow = aLayer->AsShadowLayer();
    shadow->SetShadowTransform(layerTransform);

    const nsIntRect* clipRect = aLayer->GetClipRect();
    if (clipRect) {
      nsIntRect transformedClipRect(*clipRect);
      transformedClipRect.MoveBy(aTranslation.x, aTranslation.y);
      shadow->SetShadowClipRect(&transformedClipRect);
    }
  }

  for (Layer* child = aLayer->GetFirstChild();
       child; child = child->GetNextSibling()) {
    TranslateFixedLayers(child, aTranslation);
  }
}

// Go down shadow layer tree, setting properties to match their non-shadow
// counterparts.
static void
SetShadowProperties(Layer* aLayer)
{
  // FIXME: Bug 717688 -- Do these updates in ShadowLayersParent::RecvUpdate.
  ShadowLayer* shadow = aLayer->AsShadowLayer();
  shadow->SetShadowTransform(aLayer->GetTransform());
  shadow->SetShadowVisibleRegion(aLayer->GetVisibleRegion());
  shadow->SetShadowClipRect(aLayer->GetClipRect());
  shadow->SetShadowOpacity(aLayer->GetOpacity());

  for (Layer* child = aLayer->GetFirstChild();
      child; child = child->GetNextSibling()) {
    SetShadowProperties(child);
  }
}

static void
SampleValue(float aPoint, Animation& aAnimation, nsStyleAnimation::Value& aStart,
            nsStyleAnimation::Value& aEnd, Animatable* aValue)
{
  nsStyleAnimation::Value interpolatedValue;
  NS_ASSERTION(aStart.GetUnit() == aEnd.GetUnit() ||
               aStart.GetUnit() == nsStyleAnimation::eUnit_None ||
               aEnd.GetUnit() == nsStyleAnimation::eUnit_None, "Must have same unit");
  if (aStart.GetUnit() == nsStyleAnimation::eUnit_Transform ||
      aEnd.GetUnit() == nsStyleAnimation::eUnit_Transform) {
    nsStyleAnimation::Interpolate(eCSSProperty_transform, aStart, aEnd,
                                  aPoint, interpolatedValue);
    nsCSSValueList* interpolatedList = interpolatedValue.GetCSSValueListValue();

    TransformData& data = aAnimation.data().get_TransformData();
    gfx3DMatrix transform =
      nsDisplayTransform::GetResultingTransformMatrix(nsnull, data.origin(), nsDeviceContext::AppUnitsPerCSSPixel(),
                                                      &data.bounds(), interpolatedList, &data.mozOrigin(),
                                                      &data.perspectiveOrigin(), &data.perspective());

    InfallibleTArray<TransformFunction>* functions = new InfallibleTArray<TransformFunction>();
    functions->AppendElement(TransformMatrix(transform));
    *aValue = *functions;
    return;
  }

  NS_ASSERTION(aStart.GetUnit() == nsStyleAnimation::eUnit_Float, "Should be opacity");
  nsStyleAnimation::Interpolate(eCSSProperty_opacity, aStart, aEnd,
                                aPoint, interpolatedValue);
  *aValue = interpolatedValue.GetFloatValue();
}

static void
SampleAnimations(Layer* aLayer, TimeStamp aPoint, bool* aActiveAnimation)
{
  AnimationArray& animations =
    const_cast<AnimationArray&>(aLayer->GetAnimations());
  InfallibleTArray<AnimData>& animationData =
    const_cast<InfallibleTArray<AnimData>&>(aLayer->GetAnimationData());
  for (PRInt32 i = animations.Length() - 1; i >= 0; --i) {
    Animation& animation = animations[i];
    AnimData& animData = animationData[i];

    double numIterations = animation.numIterations() != -1 ?
      animation.numIterations() : NS_IEEEPositiveInfinity();
    double positionInIteration =
      ElementAnimations::GetPositionInIteration(animation.startTime(),
                                                aPoint,
                                                animation.duration(),
                                                numIterations,
                                                animation.direction());

    if (positionInIteration == -1) {
        animations.RemoveElementAt(i);
        animationData.RemoveElementAt(i);
        continue;
    }

    NS_ABORT_IF_FALSE(0.0 <= positionInIteration &&
                          positionInIteration <= 1.0,
                        "position should be in [0-1]");

    int segmentIndex = 0;
    AnimationSegment* segment = animation.segments().Elements();
    while (segment->endPoint() < positionInIteration) {
      ++segment;
      ++segmentIndex;
    }

    double positionInSegment = (positionInIteration - segment->startPoint()) /
                                 (segment->endPoint() - segment->startPoint());

    double point = animData.mFunctions[segmentIndex]->GetValue(positionInSegment);

    *aActiveAnimation = true;

    // interpolate the property
    Animatable interpolatedValue;
    SampleValue(point, animation, animData.mStartValues[segmentIndex],
                animData.mEndValues[segmentIndex], &interpolatedValue);
    ShadowLayer* shadow = aLayer->AsShadowLayer();
    switch (interpolatedValue.type()) {
    case Animatable::TOpacity:
      shadow->SetShadowOpacity(interpolatedValue.get_Opacity().value());
      break;
    case Animatable::TArrayOfTransformFunction: {
      gfx3DMatrix matrix = interpolatedValue.get_ArrayOfTransformFunction()[0].get_TransformMatrix().value();
      gfx3DMatrix scalingMatrix = aLayer->GetScalingMatrix();
      shadow->SetShadowTransform(scalingMatrix * matrix);
      break;
    }
    default:
      NS_WARNING("Unhandled animated property");
    }
  }

  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    SampleAnimations(child, aPoint, aActiveAnimation);
  }
}

bool
CompositorParent::ApplyAsyncPanZoom(Layer* aLayer)
{
  bool foundScrollableFrame = false;
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    foundScrollableFrame |= ApplyAsyncPanZoom(child);
  }

  ContainerLayer* container = aLayer->AsContainerLayer();
  if (!container) {
    return foundScrollableFrame;
  }

  const FrameMetrics& metrics = container->GetFrameMetrics();
  const gfx3DMatrix& transform = aLayer->GetTransform();

  gfx3DMatrix treeTransform;
  gfxPoint reverseViewTranslation;

  if (metrics.IsScrollable()) {
    mAsyncPanZoomController->GetContentTransformForFrame(metrics,
                                                         transform,
                                                         mWidgetSize,
                                                         &treeTransform,
                                                         &reverseViewTranslation);
    ShadowLayer* shadow = aLayer->AsShadowLayer();
    shadow->SetShadowTransform(transform * treeTransform);
    TranslateFixedLayers(aLayer, reverseViewTranslation);
    return true;
  }

  return foundScrollableFrame;
}

void
CompositorParent::UpdateAsyncPanZoom(Layer* aLayer)
{
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    UpdateAsyncPanZoom(child);
  }

  ContainerLayer* container = aLayer->AsContainerLayer();
  if (!container) {
    return;
  }

  const FrameMetrics& metrics = container->GetFrameMetrics();
  const gfx3DMatrix& transform = aLayer->GetTransform();

  float scaleX = transform.GetXScale();

  if (metrics.IsScrollable()) {
    if (mIsFirstPaint) {
      mContentRect = metrics.mContentRect;
      SetFirstPaintViewport(metrics.mViewportScrollOffset,
                            1 / scaleX,
                            mContentRect,
                            metrics.mCSSContentRect);
    } else if (!metrics.mContentRect.IsEqualEdges(mContentRect)) {
      mContentRect = metrics.mContentRect;
      SetPageRect(metrics.mCSSContentRect);
    }

    nsIntRect displayPort = metrics.mDisplayPort;
    nsIntPoint scrollOffset = metrics.mViewportScrollOffset;
    displayPort.x += scrollOffset.x;
    displayPort.y += scrollOffset.y;

    SyncViewportInfo(displayPort, 1/scaleX, mLayersUpdated,
                     mScrollOffset, mXScale, mYScale);
    mLayersUpdated = false;
  }
}

void
CompositorParent::TransformShadowTree()
{
  Layer* layer = GetPrimaryScrollableLayer();
  ShadowLayer* shadow = layer->AsShadowLayer();
  ContainerLayer* container = layer->AsContainerLayer();
  Layer* root = mLayerManager->GetRoot();

  const FrameMetrics& metrics = container->GetFrameMetrics();
  const gfx3DMatrix& rootTransform = root->GetTransform();
  const gfx3DMatrix& currentTransform = layer->GetTransform();

  if (mAsyncPanZoomController)
    UpdateAsyncPanZoom(root);

  gfx3DMatrix treeTransform;
  gfxPoint reverseViewTranslation;

  bool activeAnimation = false;
  SampleAnimations(layer, mLastCompose, &activeAnimation);
  if (activeAnimation)
    ScheduleComposition();

#ifdef MOZ_JAVA_COMPOSITOR
  // Handle transformations for asynchronous panning and zooming. We determine the
  // zoom used by Gecko from the transformation set on the root layer, and we
  // determine the scroll offset used by Gecko from the frame metrics of the
  // primary scrollable layer. We compare this to the desired zoom and scroll
  // offset in the view transform we obtained from Java in order to compute the
  // transformation we need to apply.
  float tempScaleDiffX = rootScaleX * mXScale;
  float tempScaleDiffY = rootScaleY * mYScale;

  nsIntPoint metricsScrollOffset(0, 0);
  if (metrics.IsScrollable())
    metricsScrollOffset = metrics.mViewportScrollOffset;

  nsIntPoint scrollCompensation(
    (mScrollOffset.x / tempScaleDiffX - metricsScrollOffset.x) * mXScale,
    (mScrollOffset.y / tempScaleDiffY - metricsScrollOffset.y) * mYScale);

  treeTransform = gfx3DMatrix(ViewTransform(-scrollCompensation, mXScale, mYScale));

  float offsetX = mScrollOffset.x / tempScaleDiffX,
        offsetY = mScrollOffset.y / tempScaleDiffY;

  offsetX = NS_MAX((float)mContentRect.x,
                   NS_MIN(offsetX, (float)(mContentRect.XMost() - mWidgetSize.width)));
  offsetY = NS_MAX((float)mContentRect.y,
                    NS_MIN(offsetY, (float)(mContentRect.YMost() - mWidgetSize.height)));
  reverseViewTranslation = gfxPoint(offsetX - metricsScrollOffset.x,
                                    offsetY - metricsScrollOffset.y);
#endif

  shadow->SetShadowTransform(treeTransform * currentTransform);
  TranslateFixedLayers(layer, reverseViewTranslation);

  if (mAsyncPanZoomController) {
    // If there's a fling animation happening, advance it by 1 frame.
    mAsyncPanZoomController->DoFling();

    // Apply transforms for panning and zooming.
    bool foundScrollableFrame = ApplyAsyncPanZoom(root);

    // Inform the AsyncPanZoomController about whether or not we're compositing
    // a scrollable frame.
    mAsyncPanZoomController->SetCompositing(foundScrollableFrame);

    // If there has been a layers update in the form of a pan or zoom, then
    // signal it during synchronization.
    if (mAsyncPanZoomController->GetMetricsUpdated()) {
      mAsyncPanZoomController->ResetMetricsUpdated();
      ScheduleComposition();
    }
  }
}

void
CompositorParent::SetFirstPaintViewport(const nsIntPoint& aOffset, float aZoom,
                                        const nsIntRect& aPageRect, const gfx::Rect& aCssPageRect)
{
  if (mAsyncPanZoomController) {
    ReentrantMonitorAutoEnter monitor(mAsyncPanZoomController->GetReentrantMonitor());

    FrameMetrics metrics = mAsyncPanZoomController->GetFrameMetrics();

    metrics.mViewportScrollOffset = aOffset;
    metrics.mResolution.width = metrics.mResolution.height = aZoom;
    metrics.mContentRect = aPageRect;
    metrics.mCSSContentRect = aCssPageRect;

    mAsyncPanZoomController->SetFrameMetrics(metrics);
  }

#ifdef MOZ_WIDGET_ANDROID
  mozilla::AndroidBridge::Bridge()->SetFirstPaintViewport(aOffset, aZoom, aPageRect, aCssPageRect);
#endif
}

void
CompositorParent::SetPageRect(const gfx::Rect& aCssPageRect)
{
  if (mAsyncPanZoomController) {
    ReentrantMonitorAutoEnter monitor(mAsyncPanZoomController->GetReentrantMonitor());

    FrameMetrics metrics = mAsyncPanZoomController->GetFrameMetrics();
    metrics.mCSSContentRect = aCssPageRect;
    gfx::Rect cssContentRect = metrics.mCSSContentRect;
    float scale = metrics.mResolution.width;
    cssContentRect.x *= scale;
    cssContentRect.y *= scale;
    cssContentRect.width *= scale;
    cssContentRect.height *= scale;
    metrics.mContentRect = nsIntRect(NS_lround(cssContentRect.x),
                                     NS_lround(cssContentRect.y),
                                     NS_lround(cssContentRect.width),
                                     NS_lround(cssContentRect.height));
    mAsyncPanZoomController->SetFrameMetrics(metrics);
  }

#ifdef MOZ_WIDGET_ANDROID
  mozilla::AndroidBridge::Bridge()->SetPageRect(aCssPageRect);
#endif
}

void
CompositorParent::SyncViewportInfo(const nsIntRect& aDisplayPort,
                                   float aDisplayResolution, bool aLayersUpdated,
                                   nsIntPoint& aScrollOffset, float& aScaleX, float& aScaleY)
{
  if (mAsyncPanZoomController) {
    ReentrantMonitorAutoEnter monitor(mAsyncPanZoomController->GetReentrantMonitor());

    FrameMetrics metrics = mAsyncPanZoomController->GetFrameMetrics();

    // Update our CompositorParent copy of the viewport data.
    aScrollOffset = metrics.mViewportScrollOffset;
    aScaleX = metrics.mResolution.width;
    aScaleY = metrics.mResolution.height;

    // Send back our data, which includes the displayport, resolution and whether
    // or not the layers have been updated.
    // Don't set the resolution on the metrics because it's not relevant data.
    metrics.mDisplayPort = aDisplayPort;

    mAsyncPanZoomController->SetFrameMetrics(metrics);
  }

#ifdef MOZ_WIDGET_ANDROID
  if (mAsyncPanZoomController) {
    mozilla::AndroidBridge::Bridge()->SetViewportInfo(aDisplayPort, aDisplayResolution, aLayersUpdated,
                                                      aScrollOffset, aScaleX, aScaleY);
    if (aLayersUpdated)
        AndroidBridge::Bridge()->ForceRepaint();
  } else {
    mozilla::AndroidBridge::Bridge()->SyncViewportInfo(aDisplayPort, aDisplayResolution, aLayersUpdated,
                                                       aScrollOffset, aScaleX, aScaleY);
  }
#endif
}

void
CompositorParent::ShadowLayersUpdated(ShadowLayersParent* aLayerTree,
                                      bool isFirstPaint)
{
  mIsFirstPaint = mIsFirstPaint || isFirstPaint;
  mLayersUpdated = true;
  Layer* root = aLayerTree->GetRoot();
  mLayerManager->SetRoot(root);
  SetShadowProperties(root);
  ScheduleComposition();
}

PLayersParent*
CompositorParent::AllocPLayers(const LayersBackend& aBackendType,
                               const int64_t& aId,
                               int32_t* aMaxTextureSize)
{
  MOZ_ASSERT(aId == -1);

  // mWidget doesn't belong to the compositor thread, so it should be set to
  // NULL before returning from this method, to avoid accessing it elsewhere.
  nsIntRect rect;
  mWidget->GetBounds(rect);
  mWidgetSize.width = rect.width;
  mWidgetSize.height = rect.height;

  if (aBackendType == LayerManager::LAYERS_OPENGL) {
    nsRefPtr<LayerManagerOGL> layerManager;
    layerManager =
      new LayerManagerOGL(mWidget, mEGLSurfaceSize.width, mEGLSurfaceSize.height, mRenderToEGLSurface);
    mWidget = NULL;
    mLayerManager = layerManager;
    ShadowLayerManager* shadowManager = layerManager->AsShadowManager();
    if (shadowManager) {
      shadowManager->SetCompositorID(mCompositorID);  
    }
    
    if (!layerManager->Initialize()) {
      NS_ERROR("Failed to init OGL Layers");
      return NULL;
    }

    ShadowLayerManager* slm = layerManager->AsShadowManager();
    if (!slm) {
      return NULL;
    }
    *aMaxTextureSize = layerManager->GetMaxTextureSize();
    return new ShadowLayersParent(slm, this, -1);
  } else if (aBackendType == LayerManager::LAYERS_BASIC) {
    nsRefPtr<LayerManager> layerManager = new BasicShadowLayerManager(mWidget);
    mWidget = NULL;
    mLayerManager = layerManager;
    ShadowLayerManager* slm = layerManager->AsShadowManager();
    if (!slm) {
      return NULL;
    }
    *aMaxTextureSize = layerManager->GetMaxTextureSize();
    return new ShadowLayersParent(slm, this, -1);
  } else {
    NS_ERROR("Unsupported backend selected for Async Compositor");
    return NULL;
  }
}

bool
CompositorParent::DeallocPLayers(PLayersParent* actor)
{
  delete actor;
  return true;
}

void
CompositorParent::SetAsyncPanZoomController(AsyncPanZoomController* aAsyncPanZoomController)
{
  mAsyncPanZoomController = aAsyncPanZoomController;
}

namespace {
  typedef std::map<PRUint32,CompositorParent*> CompositorMap;
  CompositorMap* sCompositorMap;
} // anonymous namespace

void CompositorParent::CreateCompositorMap()
{
  if (sCompositorMap == nsnull) {
    sCompositorMap = new CompositorMap;
  }
}

void CompositorParent::DestroyCompositorMap()
{
  if (sCompositorMap != nsnull) {
    delete sCompositorMap;
    sCompositorMap = nsnull;
  }
}

CompositorParent* CompositorParent::GetCompositor(PRUint32 id)
{
  CompositorMap::iterator it = sCompositorMap->find(id);
  if (it == sCompositorMap->end()) return nsnull;
  return it->second;
}

PRUint32 CompositorParent::AddCompositor(CompositorParent* compositor)
{
  static PRUint32 sNextID = 1;
  while ((sNextID == 0) || (sCompositorMap->find(sNextID) != sCompositorMap->end())) {
    ++sNextID;
  }

  (*sCompositorMap)[sNextID] = compositor;
  return sNextID;
}

CompositorParent* CompositorParent::RemoveCompositor(PRUint32 id)
{
  CompositorMap::iterator it = sCompositorMap->find(id);
  if (it == sCompositorMap->end()) return nsnull;
  sCompositorMap->erase(it);
  return it->second;
}
 
typedef map<int64_t, RefPtr<Layer> > LayerTreeMap;
static LayerTreeMap sIndirectLayerTrees;

/*static*/ int64_t
CompositorParent::AllocateLayerTreeId()
{
  MOZ_ASSERT(CompositorLoop());
  MOZ_ASSERT(NS_IsMainThread());
  static int64_t ids;
  return ++ids;
}

/** */
class CrossProcessCompositorParent : public PCompositorParent,
                                     public ShadowLayersManager
{
  friend class CompositorParent;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CrossProcessCompositorParent)
public:
  CrossProcessCompositorParent() {}
  virtual ~CrossProcessCompositorParent() {}

  virtual void ActorDestroy(ActorDestroyReason aWhy) MOZ_OVERRIDE;

  virtual bool RecvWillStop() MOZ_OVERRIDE;
  virtual bool RecvStop() MOZ_OVERRIDE;
  virtual bool RecvPause() MOZ_OVERRIDE;
  virtual bool RecvResume() MOZ_OVERRIDE;

  virtual PLayersParent* AllocPLayers(const LayersBackend& aBackendType,
                                      const int64_t& aId,
                                      int32_t* aMaxTextureSize) MOZ_OVERRIDE;
  virtual bool DeallocPLayers(PLayersParent* aLayers) MOZ_OVERRIDE;

  virtual void ShadowLayersUpdated(ShadowLayersParent* aLayerTree,
                                   bool isFirstPaint) MOZ_OVERRIDE;

private:
  void DeferredDestroy();

  nsRefPtr<CrossProcessCompositorParent> mSelfRef;
};

static void
OpenCompositor(CrossProcessCompositorParent* aCompositor,
               Transport* aTransport, ProcessHandle aHandle,
               MessageLoop* aIOLoop)
{
  DebugOnly<bool> ok = aCompositor->Open(aTransport, aHandle, aIOLoop);
  MOZ_ASSERT(ok);
}

/*static*/ PCompositorParent*
CompositorParent::Create(Transport* aTransport, ProcessId aOtherProcess)
{
  nsRefPtr<CrossProcessCompositorParent> cpcp =
    new CrossProcessCompositorParent();
  ProcessHandle handle;
  if (!base::OpenProcessHandle(aOtherProcess, &handle)) {
    // XXX need to kill |aOtherProcess|, it's boned
    return nsnull;
  }
  cpcp->mSelfRef = cpcp;
  CompositorLoop()->PostTask(
    FROM_HERE,
    NewRunnableFunction(OpenCompositor, cpcp.get(),
                        aTransport, handle, XRE_GetIOMessageLoop()));
  // This is a little scary but we promise to be good.  The return
  // value is just compared to null for success checking; the value
  // itself isn't used.  If that ever changes, we'll crash, hard.
  return reinterpret_cast<PCompositorParent*>(1);
}

static void
UpdateIndirectTree(int64_t aId, Layer* aRoot)
{
  sIndirectLayerTrees[aId] = aRoot;
}

static Layer*
GetIndirectShadowTree(int64_t aId)
{
  LayerTreeMap::const_iterator cit = sIndirectLayerTrees.find(aId);
  if (sIndirectLayerTrees.end() == cit) {
    return nsnull;
  }
  return (*cit).second;
}

static void
RemoveIndirectTree(int64_t aId)
{
  sIndirectLayerTrees.erase(aId);
}

void
CrossProcessCompositorParent::ActorDestroy(ActorDestroyReason aWhy)
{
  MessageLoop::current()->PostTask(
    FROM_HERE,
    NewRunnableMethod(this, &CrossProcessCompositorParent::DeferredDestroy));
}

bool
CrossProcessCompositorParent::RecvWillStop()
{
  // TODO
  return true;
}

bool
CrossProcessCompositorParent::RecvStop()
{
  // TODO
  return true;
}

bool
CrossProcessCompositorParent::RecvPause()
{
  // TODO
  return true;
}

bool
CrossProcessCompositorParent::RecvResume()
{
  // TODO
  return true;
}

PLayersParent*
CrossProcessCompositorParent::AllocPLayers(const LayersBackend& aBackendType,
                                           const int64_t& aId,
                                           int32_t* aMaxTextureSize)
{
  MOZ_ASSERT(aId != -1);

  nsRefPtr<LayerManager> lm = sCurrent->GetLayerManager();
  *aMaxTextureSize = lm->GetMaxTextureSize();
  return new ShadowLayersParent(lm->AsShadowManager(), this, aId);
}

bool
CrossProcessCompositorParent::DeallocPLayers(PLayersParent* aLayers)
{
  ShadowLayersParent* slp = static_cast<ShadowLayersParent*>(aLayers);
  RemoveIndirectTree(slp->GetId());
  delete aLayers;
  return true;
}

void
CrossProcessCompositorParent::ShadowLayersUpdated(ShadowLayersParent* aLayerTree,
                                                  bool isFirstPaint)
{
  int64_t id = aLayerTree->GetId();
  MOZ_ASSERT(id != -1);
  Layer* shadowRoot = aLayerTree->GetRoot();
  if (shadowRoot) {
    SetShadowProperties(shadowRoot);
  }
  UpdateIndirectTree(id, shadowRoot);
}

void
CrossProcessCompositorParent::DeferredDestroy()
{
  mSelfRef = NULL;
  // |this| was just destroyed, hands off
}

} // namespace layers
} // namespace mozilla

