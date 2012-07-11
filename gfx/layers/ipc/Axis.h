/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_Axis_h
#define mozilla_layers_Axis_h

#include "nsGUIEvent.h"

namespace mozilla {
namespace layers {

class AsyncPanZoomController;

/**
 * Helper class to maintain each axis of movement (X,Y) for panning and zooming.
 * Note that everything here is specific to one axis; that is, the X axis knows
 * nothing about the Y axis and vice versa.
 */
class Axis {
public:
  Axis(AsyncPanZoomController* aAsyncPanZoomController);

  enum Overscroll {
    OVERSCROLL_NONE = 0,
    OVERSCROLL_MINUS,
    OVERSCROLL_PLUS,
    OVERSCROLL_BOTH
  };

  /**
   * Helper for float precision correction.
   */
  static const float EPSILON;

  /**
   * Milliseconds per frame, used to judge how much displacement should have
   * happened every frame based on the velocity calculated from touch events.
   */
  static const float MS_PER_FRAME;

  /**
   * Maximum acceleration that can happen between two frames. Velocity is
   * throttled if it's above this. This may happen if a time delta is very low,
   * or we get a touch point very far away from the previous position for some
   * reason.
   */
  static const float MAX_EVENT_ACCELERATION;

  /**
   * Amount of friction applied during flings when going above
   * VELOCITY_THRESHOLD.
   */
  static const float FLING_FRICTION_FAST;

  /**
   * Amount of friction applied during flings when going below
   * VELOCITY_THRESHOLD.
   */
  static const float FLING_FRICTION_SLOW;

  /**
   * Maximum velocity before fling friction increases.
   */
  static const float VELOCITY_THRESHOLD;

  /**
   * When flinging, if the velocity goes below this number, we just stop the
   * animation completely. This is to prevent asymptotically approaching 0
   * velocity and rerendering unnecessarily.
   */
  static const float FLING_STOPPED_THRESHOLD;

  /**
   * Maximum distance that we allow for edge resistance, specifically for
   * bouncing. We asymptotically approach this but should never go above it.
   */
  static const float SNAP_LIMIT;

  /**
   * Notify this Axis that a new touch has been received, including a time delta
   * indicating how long it has been since the previous one. This triggers a
   * recalculation of velocity.
   */
  void UpdateWithTouchAtDevicePoint(PRInt32 pos, PRInt32 timeDelta);

  /**
   * Notify this Axis that a touch has begun, i.e. the user has put their finger
   * on the screen but has not yet tried to pan.
   */
  void StartTouch(PRInt32 pos);

  /**
   * Notify this Axis that a touch has ended. Useful for stopping flings when a
   * user puts their finger down in the middle of one.
   */
  void StopTouch();

  /**
   * Gets displacement that should have happened since the previous touch.
   * Note: Does not reset the displacement. It gets recalculated on the next
   * updateWithTouchAtDevicePoint(), however it is not safe to assume this will
   * be the same on every call. This also checks for page boundaries and will
   * return an adjusted displacement to prevent the viewport from overscrolling
   * the page rect. An example of where this might matter is when you call it,
   * apply a displacement that takes you to the boundary of the page, then call
   * it again. The result will be different in this case.
   */
  PRInt32 UpdateAndGetDisplacement(float scale);

  /**
   * Gets the distance between the starting position of the touch supplied in
   * startTouch() and the current touch from the last
   * updateWithTouchAtDevicePoint().
   */
  float PanDistance();

  /**
   * Applies friction during a fling, or cancels the fling if the velocity is
   * too low. Returns true if the fling should continue to another frame, or
   * false if it should end.
   */
  bool FlingApplyFrictionOrCancel();

  /**
   * Gets the overscroll state of the axis in its current position.
   */
  Overscroll GetOverscroll();

  /**
   * If there is overscroll, returns the amount. Sign depends on in what
   * direction it is overflowing. Positive excess means that it is overflowing
   * in the positive direction, whereas negative excess means that it is
   * overflowing in the negative direction.
   */
  PRInt32 GetExcess();

  /**
   * Gets the raw velocity of this axis at this moment.
   */
  float GetVelocity();

  /**
   * Gets the overscroll state of the axis given an additional displacement.
   * That is to say, if the given displacement is applied, this will tell you
   * whether or not it will overscroll, and in what direction.
   */
  Overscroll DisplacementWillOverscroll(PRInt32 displacement);

  /**
   * If a displacement will overflow the axis, this returns the amount and in
   * what direction. Similar to getExcess() but takes a displacement to apply.
   */
  PRInt32 DisplacementWillOverscrollAmount(PRInt32 displacement);

  /**
   * Gets the overscroll state of the axis given a scaling of the page. That is
   * to say, if the given scale is applied, this will tell you whether or not
   * it will overscroll, and in what direction.
   */
  Overscroll ScaleWillOverscroll(float scale, PRInt32 focus);

  /**
   * If a scale will overflow the axis, this returns the amount and in what
   * direction. Similar to getExcess() but takes a displacement to apply.
   */
  PRInt32 ScaleWillOverscrollAmount(float scale, PRInt32 focus);

  virtual PRInt32 GetOrigin() = 0;
  virtual PRInt32 GetViewportLength() = 0;
  virtual PRInt32 GetPageStart() = 0;
  virtual PRInt32 GetPageLength() = 0;
  /**
   * Checks if an axis will overscroll in both directions by computing the
   * content rect and checking that its height/width (depending on the axis)
   * does not overextend past the viewport.
   */
  virtual bool ScaleWillOverscrollBothWays(float scale) = 0;
  PRInt32 GetViewportEnd();
  PRInt32 GetPageEnd();

protected:
  PRInt32 mPos;
  PRInt32 mStartPos;
  float mVelocity;
  nsRefPtr<AsyncPanZoomController> mAsyncPanZoomController;
};

class AxisX : public Axis {
public:
  AxisX(AsyncPanZoomController* mAsyncPanZoomController);
  PRInt32 GetOrigin();
  PRInt32 GetViewportLength();
  PRInt32 GetPageStart();
  PRInt32 GetPageLength();
  bool ScaleWillOverscrollBothWays(float scale);
};

class AxisY : public Axis {
public:
  AxisY(AsyncPanZoomController* mAsyncPanZoomController);
  PRInt32 GetOrigin();
  PRInt32 GetViewportLength();
  PRInt32 GetPageStart();
  PRInt32 GetPageLength();
  bool ScaleWillOverscrollBothWays(float scale);
};

}
}

#endif
