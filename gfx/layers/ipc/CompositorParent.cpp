/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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

using base::Thread;
namespace mozilla {
namespace layers {

CompositorParent::CompositorParent(nsIWidget* aWidget, MessageLoop* aMsgLoop,
                                   PlatformThreadId aThreadID, bool aRenderToEGLSurface,
                                   int aSurfaceWidth, int aSurfaceHeight)
  : mWidget(aWidget)
  , mCurrentCompositeTask(NULL)
  , mPaused(false)
  , mXScale(1.0)
  , mYScale(1.0)
  , mIsFirstPaint(false)
  , mLayersUpdated(false)
  , mCompositorLoop(aMsgLoop)
  , mThreadID(aThreadID)
  , mRenderToEGLSurface(aRenderToEGLSurface)
  , mEGLSurfaceSize(aSurfaceWidth, aSurfaceHeight)
  , mPauseCompositionMonitor("PauseCompositionMonitor")
  , mResumeCompositionMonitor("ResumeCompositionMonitor")
{
  MOZ_COUNT_CTOR(CompositorParent);
}

MessageLoop*
CompositorParent::CompositorLoop()
{
  return mCompositorLoop;
}

PlatformThreadId
CompositorParent::CompositorThreadID()
{
  return mThreadID;
}

CompositorParent::~CompositorParent()
{
  MOZ_COUNT_DTOR(CompositorParent);
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
  NS_ABORT_IF_FALSE(CompositorThreadID() == PlatformThread::CurrentId(),
                    "PauseComposition() can only be called on the compositor thread");

  mozilla::MonitorAutoLock lock(mPauseCompositionMonitor);

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
  NS_ABORT_IF_FALSE(CompositorThreadID() == PlatformThread::CurrentId(),
                    "ResumeComposition() can only be called on the compositor thread");

  mozilla::MonitorAutoLock lock(mResumeCompositionMonitor);

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
  mozilla::MonitorAutoLock lock(mPauseCompositionMonitor);

  CancelableTask *pauseTask = NewRunnableMethod(this,
                                                &CompositorParent::PauseComposition);
  CompositorLoop()->PostTask(FROM_HERE, pauseTask);

  // Wait until the pause has actually been processed by the compositor thread
  lock.Wait();
}

void
CompositorParent::ScheduleResumeOnCompositorThread(int width, int height)
{
  mozilla::MonitorAutoLock lock(mResumeCompositionMonitor);

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

void
CompositorParent::Composite()
{
  NS_ABORT_IF_FALSE(CompositorThreadID() == PlatformThread::CurrentId(),
                    "Composite can only be called on the compositor thread");
  mCurrentCompositeTask = NULL;

  mLastCompose = mozilla::TimeStamp::Now();

  if (mPaused || !mLayerManager || !mLayerManager->GetRoot()) {
    return;
  }

  TransformShadowTree();

  Layer* layer = mLayerManager->GetRoot();
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

static nsCSSValueList*
CreateCSSValueList(InfallibleTArray<TransformFunction>& aFunctions)
{
  nsAutoPtr<nsCSSValueList> result;
  nsCSSValueList** resultTail = getter_Transfers(result);
  for (PRUint32 i = 0; i < aFunctions.Length(); i++) {
    nsRefPtr<nsCSSValue::Array> arr;
    switch(aFunctions[i].type()) {
      case TransformFunction::TRotation:
      {
        // The CSS spec doesn't recognize rotate3d as a primitive, so we must convert rotations
        // to the correct axis if possible in order to get correct interpolation
        float x = aFunctions[i].get_Rotation().x();
        float y = aFunctions[i].get_Rotation().y();
        float z = aFunctions[i].get_Rotation().z();
        float theta = aFunctions[i].get_Rotation().radians();
        if (x == 1 && y == 0 && z == 0) {
          arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_rotatex, resultTail);
          arr->Item(1).SetFloatValue(theta, eCSSUnit_Radian);
        } else if (x == 0 && y == 1 && z == 0) {
          arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_rotatey, resultTail);
          arr->Item(1).SetFloatValue(theta, eCSSUnit_Radian);
        } else if (x == 0 && y == 0 && z == 1) {
          arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_rotatez, resultTail);
          arr->Item(1).SetFloatValue(theta, eCSSUnit_Radian);
        } else {
          arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_rotate3d, resultTail);
          arr->Item(1).SetFloatValue(x, eCSSUnit_Number);
          arr->Item(2).SetFloatValue(y, eCSSUnit_Number);
          arr->Item(3).SetFloatValue(z, eCSSUnit_Number);
          arr->Item(4).SetFloatValue(theta, eCSSUnit_Radian);
        }
        break;
      }
      case TransformFunction::TScale:
      {
        arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_scale3d, resultTail);
        arr->Item(1).SetFloatValue(aFunctions[i].get_Scale().x(), eCSSUnit_Number);
        arr->Item(2).SetFloatValue(aFunctions[i].get_Scale().y(), eCSSUnit_Number);
        arr->Item(3).SetFloatValue(aFunctions[i].get_Scale().z(), eCSSUnit_Number);
        break;
      }
      case TransformFunction::TTranslation:
      {
        arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_translate3d, resultTail);
        arr->Item(1).SetFloatValue(aFunctions[i].get_Translation().x(), eCSSUnit_Number);
        arr->Item(2).SetFloatValue(aFunctions[i].get_Translation().y(), eCSSUnit_Number);
        arr->Item(3).SetFloatValue(aFunctions[i].get_Translation().z(), eCSSUnit_Number);
        break;
      }
      case TransformFunction::TSkew:
      {
        float x = aFunctions[i].get_Skew().x();
        float y = aFunctions[i].get_Skew().y();
        if (y == 0) {
          arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_skewx, resultTail);
          arr->Item(1).SetFloatValue(x, eCSSUnit_Number);
        } else {
          arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_skewy, resultTail);
          arr->Item(1).SetFloatValue(y, eCSSUnit_Number);
        }
      }
      case TransformFunction::TTransformMatrix:
      {
        arr = nsStyleAnimation::AppendTransformFunction(eCSSKeyword_matrix3d, resultTail);
        gfx3DMatrix& matrix = aFunctions[i].get_TransformMatrix().value();
        arr->Item(1).SetFloatValue(matrix._11, eCSSUnit_Number);
        arr->Item(2).SetFloatValue(matrix._12, eCSSUnit_Number);
        arr->Item(3).SetFloatValue(matrix._13, eCSSUnit_Number);
        arr->Item(4).SetFloatValue(matrix._14, eCSSUnit_Number);
        arr->Item(5).SetFloatValue(matrix._21, eCSSUnit_Number);
        arr->Item(6).SetFloatValue(matrix._22, eCSSUnit_Number);
        arr->Item(7).SetFloatValue(matrix._23, eCSSUnit_Number);
        arr->Item(8).SetFloatValue(matrix._24, eCSSUnit_Number);
        arr->Item(9).SetFloatValue(matrix._31, eCSSUnit_Number);
        arr->Item(10).SetFloatValue(matrix._32, eCSSUnit_Number);
        arr->Item(11).SetFloatValue(matrix._33, eCSSUnit_Number);
        arr->Item(12).SetFloatValue(matrix._34, eCSSUnit_Number);
        arr->Item(13).SetFloatValue(matrix._41, eCSSUnit_Number);
        arr->Item(14).SetFloatValue(matrix._42, eCSSUnit_Number);
        arr->Item(15).SetFloatValue(matrix._43, eCSSUnit_Number);
        arr->Item(16).SetFloatValue(matrix._44, eCSSUnit_Number);
        break;
      }
      default:
        NS_ASSERTION(false, "All functions should be implemented?");
    }
  }
  return result.forget();
}

static void
SampleValue(const AnimationSegment* aSegment, float aPoint, Animation& aAnimation, Animatable* aValue)
{
  if (aSegment->endState().type() == Animatable::TArrayOfTransformFunction) {
    InfallibleTArray<TransformFunction> startFunctions = aSegment->startState().get_ArrayOfTransformFunction();
    nsCSSValueList* startList = CreateCSSValueList(startFunctions);
    nsStyleAnimation::Value startValue;
    startValue.SetAndAdoptCSSValueListValue(startList, nsStyleAnimation::eUnit_Transform);

    InfallibleTArray<TransformFunction> endFunctions = aSegment->endState().get_ArrayOfTransformFunction();
    nsCSSValueList* endList = CreateCSSValueList(endFunctions);
    nsStyleAnimation::Value endValue;
    endValue.SetAndAdoptCSSValueListValue(endList, nsStyleAnimation::eUnit_Transform);

    nsStyleAnimation::Value interpolatedValue;
    nsStyleAnimation::Interpolate(eCSSProperty_transform, startValue, endValue, aPoint, interpolatedValue);
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

  NS_ASSERTION(aSegment->endState().type() == Animatable::TOpacity, "Should be opacity");
  float first = aSegment->startState().get_Opacity().value();
  float last = aSegment->endState().get_Opacity().value();
  *aValue = first + (last - first) * aPoint;
}

static void
SampleAnimations(Layer* aLayer, TimeStamp aPoint, bool* aActiveAnimation)
{
  AnimationArray& animations = const_cast<AnimationArray&>(aLayer->GetAnimations());
  InfallibleTArray<InfallibleTArray<css::ComputedTimingFunction*>*>& functions =
    const_cast<InfallibleTArray<InfallibleTArray<css::ComputedTimingFunction*>*>&>(aLayer->GetFunctions());
  for (PRInt32 i = animations.Length() - 1; i >= 0; --i) {
    Animation& animation = animations[i];

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
        functions.RemoveElementAt(i);
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

    double point = functions.ElementAt(i)->ElementAt(segmentIndex)->GetValue(positionInSegment);

    *aActiveAnimation = true;

    // interpolate the property
    Animatable interpolatedValue;
    SampleValue(segment, point, animation, &interpolatedValue);

    ShadowLayer* shadow = aLayer->AsShadowLayer();
    switch (interpolatedValue.type()) {
    case Animatable::TColor:
      // TODO
      NS_NOTREACHED("Don't animate color yet");
      break;
    case Animatable::TOpacity:
      shadow->SetShadowOpacity(interpolatedValue.get_Opacity().value());
      break;
    case Animatable::TArrayOfTransformFunction: {
      gfx3DMatrix matrix = interpolatedValue.get_ArrayOfTransformFunction()[0].get_TransformMatrix().value();
      shadow->SetShadowTransform(matrix);
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

void
CompositorParent::TransformShadowTree()
{
  Layer* layer = GetPrimaryScrollableLayer();
  ShadowLayer* shadow = layer->AsShadowLayer();
  ContainerLayer* container = layer->AsContainerLayer();

  const FrameMetrics& metrics = container->GetFrameMetrics();
  const gfx3DMatrix& rootTransform = mLayerManager->GetRoot()->GetTransform();
  const gfx3DMatrix& currentTransform = layer->GetTransform();

  float rootScaleX = rootTransform.GetXScale();
  float rootScaleY = rootTransform.GetYScale();

  if (mIsFirstPaint) {
    mContentRect = metrics.mContentRect;
    SetFirstPaintViewport(metrics.mViewportScrollOffset,
                          1/rootScaleX,
                          mContentRect,
                          metrics.mCSSContentRect);
    mIsFirstPaint = false;
  } else if (!metrics.mContentRect.IsEqualEdges(mContentRect)) {
    mContentRect = metrics.mContentRect;
    SetPageRect(metrics.mCSSContentRect);
  }

  if (mAsyncPanZoomController) {
    // If there's a fling animation happening, advance it by 1 frame.
    mAsyncPanZoomController->DoFling();

    // If there has been a layers update in the form of a pan or zoom, then
    // signal it during synchronization.
    if (mAsyncPanZoomController->GetLayersUpdated()) {
      mLayersUpdated = true;
      mAsyncPanZoomController->ResetLayersUpdated();
    }
  }

  // We synchronise the viewport information with Java after sending the above
  // notifications, so that Java can take these into account in its response.
  // Calculate the absolute display port to send to Java
  nsIntRect displayPort = metrics.mDisplayPort;
  nsIntPoint scrollOffset = metrics.mViewportScrollOffset;
  displayPort.x += scrollOffset.x;
  displayPort.y += scrollOffset.y;

  SyncViewportInfo(displayPort, 1/rootScaleX, mLayersUpdated,
                   mScrollOffset, mXScale, mYScale);
  mLayersUpdated = false;

  gfx3DMatrix treeTransform;
  gfxPoint reverseViewTranslation;

  if (mAsyncPanZoomController) {
    mAsyncPanZoomController->GetContentTransformForFrame(metrics,
                                                         rootTransform,
                                                         mWidgetSize,
                                                         &treeTransform,
                                                         &reverseViewTranslation);
  } else {
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
  }

  shadow->SetShadowTransform(treeTransform * currentTransform);

  TranslateFixedLayers(layer, reverseViewTranslation);

  bool activeAnimation = false;
  SampleAnimations(layer, mLastCompose, &activeAnimation);
  if (activeAnimation)
    ScheduleComposition();
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
CompositorParent::ShadowLayersUpdated(bool isFirstPaint)
{
  mIsFirstPaint = mIsFirstPaint || isFirstPaint;
  mLayersUpdated = true;
  const nsTArray<PLayersParent*>& shadowParents = ManagedPLayersParent();
  NS_ABORT_IF_FALSE(shadowParents.Length() <= 1,
                    "can only support at most 1 ShadowLayersParent");
  if (shadowParents.Length()) {
    Layer* root = static_cast<ShadowLayersParent*>(shadowParents[0])->GetRoot();
    mLayerManager->SetRoot(root);
    SetShadowProperties(root);
  }
  ScheduleComposition();
}

PLayersParent*
CompositorParent::AllocPLayers(const LayersBackend& aBackendType, int* aMaxTextureSize)
{
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

    if (!layerManager->Initialize()) {
      NS_ERROR("Failed to init OGL Layers");
      return NULL;
    }

    ShadowLayerManager* slm = layerManager->AsShadowManager();
    if (!slm) {
      return NULL;
    }
    *aMaxTextureSize = layerManager->GetMaxTextureSize();
    return new ShadowLayersParent(slm, this);
  } else if (aBackendType == LayerManager::LAYERS_BASIC) {
    nsRefPtr<LayerManager> layerManager = new BasicShadowLayerManager(mWidget);
    mWidget = NULL;
    mLayerManager = layerManager;
    ShadowLayerManager* slm = layerManager->AsShadowManager();
    if (!slm) {
      return NULL;
    }
    *aMaxTextureSize = layerManager->GetMaxTextureSize();
    return new ShadowLayersParent(slm, this);
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

} // namespace layers
} // namespace mozilla

