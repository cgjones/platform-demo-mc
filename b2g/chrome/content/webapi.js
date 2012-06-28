/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- /
/* vim: set shiftwidth=2 tabstop=2 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

'use strict';

dump('======================= webapi.js ======================= \n');

let { classes: Cc, interfaces: Ci, utils: Cu }  = Components;
Cu.import('resource://gre/modules/XPCOMUtils.jsm');
Cu.import('resource://gre/modules/Services.jsm');
Cu.import('resource://gre/modules/Geometry.jsm');

const ContentPanning = {
  init: function cp_init() {
    ['mousedown', 'mouseup', 'mousemove'].forEach(function(type) {
      addEventListener(type, ContentPanning, true);
    });
  },

  handleEvent: function cp_handleEvent(evt) {
    switch (evt.type) {
      case 'mousedown':
        this.onTouchStart(evt);
        break;
      case 'mousemove':
        this.onTouchMove(evt);
        break;
      case 'mouseup':
        this.onTouchEnd(evt);
        break;
      case 'click':
        evt.stopPropagation();
        evt.preventDefault();
        
        let target = evt.target;
        let view = target.ownerDocument ? target.ownerDocument.defaultView
                                        : target;
        view.removeEventListener('click', this, true, true);
        break;
    }
  },

  position: new Point(0 , 0),

  onTouchStart: function cp_onTouchStart(evt) {
    this.dragging = true;
    this.panning = false;

    let oldTarget = this.target;
    [this.target, this.scrollCallback] = this.getPannable(evt.target);

    // If there is a pan animation running (from a previous pan gesture) and
    // the user touch back the screen, stop this animation immediatly and
    // prevent the possible click action if the touch happens on the same
    // target.
    this.preventNextClick = false;
    if (KineticPanning.active) {
      KineticPanning.stop();

      if (oldTarget && oldTarget == this.target)
        this.preventNextClick = true;
    }


    this.position.set(evt.screenX, evt.screenY);
    KineticPanning.record(new Point(0, 0), evt.timeStamp);
  },

  onTouchEnd: function cp_onTouchEnd(evt) {
    if (!this.dragging)
      return;
    this.dragging = false;

    this.onTouchMove(evt);

    let click = evt.detail;
    if (this.target && click && (this.panning || this.preventNextClick)) {
      let target = this.target;
      let view = target.ownerDocument ? target.ownerDocument.defaultView
                                      : target;
      view.addEventListener('click', this, true, true);
    }

    if (this.panning)
      KineticPanning.start(this);
  },

  onTouchMove: function cp_onTouchMove(evt) {
    if (!this.dragging || !this.scrollCallback)
      return;

    let current = this.position;
    let delta = new Point(evt.screenX - current.x, evt.screenY - current.y);
    current.set(evt.screenX, evt.screenY);

    KineticPanning.record(delta, evt.timeStamp);
    this.scrollCallback(delta.scale(-1));

    // If a pan action happens, cancel the active state of the
    // current target.
    if (!this.panning && KineticPanning.isPan()) {
      this.panning = true;
      this._resetActive();
    }
  },


  onKineticBegin: function cp_onKineticBegin(evt) {
  },

  onKineticPan: function cp_onKineticPan(delta) {
    return !this.scrollCallback(delta);
  },

  onKineticEnd: function cp_onKineticEnd() {
    if (!this.dragging)
      this.scrollCallback = null;
  },

  getPannable: function cp_getPannable(node) {
    if (!(node instanceof Ci.nsIDOMHTMLElement) || node.tagName == 'HTML')
      return [null, null];

    let content = node.ownerDocument.defaultView;
    while (!(node instanceof Ci.nsIDOMHTMLBodyElement)) {
      let style = content.getComputedStyle(node, null);

      let overflow = [style.getPropertyValue('overflow'),
                      style.getPropertyValue('overflow-x'),
                      style.getPropertyValue('overflow-y')];

      let rect = node.getBoundingClientRect();
      let isAuto = (overflow.indexOf('auto') != -1 &&
                   (rect.height < node.scrollHeight ||
                    rect.width < node.scrollWidth));

      let isScroll = (overflow.indexOf('scroll') != -1);
      if (isScroll || isAuto)
        return [node, this._generateCallback(node)];

      node = node.parentNode;
    }

    return [content, this._generateCallback(content)];
  },

  _generateCallback: function cp_generateCallback(content) {
    function scroll(delta) {
      if (content instanceof Ci.nsIDOMHTMLElement) {
        let oldX = content.scrollLeft, oldY = content.scrollTop;
        content.scrollLeft += delta.x;
        content.scrollTop += delta.y;
        let newX = content.scrollLeft, newY = content.scrollTop;
        return (newX != oldX) || (newY != oldY);
      } else {
        let oldX = content.scrollX, oldY = content.scrollY;
        content.scrollBy(delta.x, delta.y);
        let newX = content.scrollX, newY = content.scrollY;
        return (newX != oldX) || (newY != oldY);
      }
    }
    return scroll;
  },

  get _domUtils() {
    delete this._domUtils;
    return this._domUtils = Cc['@mozilla.org/inspector/dom-utils;1']
                              .getService(Ci.inIDOMUtils);
  },

  _resetActive: function cp_resetActive() {
    let root = this.target.ownerDocument || this.target.document;

    const kStateActive = 0x00000001;
    this._domUtils.setContentState(root.documentElement, kStateActive);
  }
};

ContentPanning.init();

// Min/max velocity of kinetic panning. This is in pixels/millisecond.
const kMinVelocity = 0.4;
const kMaxVelocity = 6;

// Constants that affect the "friction" of the scroll pane.
const kExponentialC = 1000;
const kPolynomialC = 100 / 1000000;

// How often do we change the position of the scroll pane?
// Too often and panning may jerk near the end.
// Too little and panning will be choppy. In milliseconds.
const kUpdateInterval = 16;

// The numbers of momentums to use for calculating the velocity of the pan.
// Those are taken from the end of the action
const kSamples = 5;

const KineticPanning = {
  _position: new Point(0, 0),
  _velocity: new Point(0, 0),
  _acceleration: new Point(0, 0),

  get active() {
    return this.target !== null;
  },

  target: null,
  start: function kp_start(target) {
    this.target = target;

    // Calculate the initial velocity of the movement based on user input
    let momentums = this.momentums.slice(-kSamples);

    let distance = new Point(0, 0);
    momentums.forEach(function(momentum) {
      distance.add(momentum.dx, momentum.dy);
    });

    let elapsed = momentums[momentums.length - 1].time - momentums[0].time;

    function clampFromZero(x, min, max) {
      if (x >= 0)
        return Math.max(min, Math.min(max, x));
      return Math.min(-min, Math.max(-max, x));
    }

    let velocityX = clampFromZero(distance.x / elapsed, 0, kMaxVelocity);
    let velocityY = clampFromZero(distance.y / elapsed, 0, kMaxVelocity);

    let velocity = this._velocity;
    velocity.set(Math.abs(velocityX) < kMinVelocity ? 0 : velocityX,
                 Math.abs(velocityY) < kMinVelocity ? 0 : velocityY);
    this.momentums = [];

    // Set acceleration vector to opposite signs of velocity
    function sign(x) {
      return x ? (x > 0 ? 1 : -1) : 0;
    }

    this._acceleration.set(velocity.clone().map(sign).scale(-kPolynomialC));

    // Reset the position
    this._position.set(0, 0);

    this._startAnimation();

    this.target.onKineticBegin();
  },

  stop: function kp_stop() {
    if (!this.target)
      return;

    this.momentums = [];
    this.distance.set(0, 0);

    this.target.onKineticEnd();
    this.target = null;
  },

  momentums: [],
  record: function kp_record(delta, timestamp) {
    this.momentums.push({ 'time': timestamp, 'dx' : delta.x, 'dy' : delta.y });
    this.distance.add(delta.x, delta.y);
  },

  get threshold() {
    let dpi = content.QueryInterface(Ci.nsIInterfaceRequestor)
                     .getInterface(Ci.nsIDOMWindowUtils)
                     .displayDPI;

    let threshold = Services.prefs.getIntPref('ui.dragThresholdX') / 240 * dpi;

    delete this.threshold;
    return this.threshold = threshold;
  },

  distance: new Point(0, 0),
  isPan: function cp_isPan() {
    return (Math.abs(this.distance.x) > this.threshold ||
            Math.abs(this.distance.y) > this.threshold);
  },

  _startAnimation: function kp_startAnimation() {
    let c = kExponentialC;
    function getNextPosition(position, v, a, t) {
      // Important traits for this function:
      //   p(t=0) is 0
      //   p'(t=0) is v0
      //
      // We use exponential to get a smoother stop, but by itself exponential
      // is too smooth at the end. Adding a polynomial with the appropriate
      // weight helps to balance
      position.set(v.x * Math.exp(-t / c) * -c + a.x * t * t + v.x * c,
                   v.y * Math.exp(-t / c) * -c + a.y * t * t + v.y * c);
    }

    let startTime = content.mozAnimationStartTime;
    let elapsedTime = 0, targetedTime = 0, averageTime = 0;

    let velocity = this._velocity;
    let acceleration = this._acceleration;

    let position = this._position;
    let nextPosition = new Point(0, 0);
    let delta = new Point(0, 0);

    let callback = (function(timestamp) {
      if (!this.target)
        return;

      // To make animation end fast enough but to keep smoothness, average the
      // ideal time frame (smooth animation) with the actual time lapse
      // (end fast enough).
      // Animation will never take longer than 2 times the ideal length of time.
      elapsedTime = timestamp - startTime;
      targetedTime += kUpdateInterval;
      averageTime = (targetedTime + elapsedTime) / 2;

      // Calculate new position.
      getNextPosition(nextPosition, velocity, acceleration, averageTime);
      delta.set(Math.round(nextPosition.x - position.x),
                Math.round(nextPosition.y - position.y));

      // Test to see if movement is finished for each component.
      if (delta.x * acceleration.x > 0)
        delta.x = position.x = velocity.x = acceleration.x = 0;

      if (delta.y * acceleration.y > 0)
        delta.y = position.y = velocity.y = acceleration.y = 0;

      if (velocity.equals(0, 0) || delta.equals(0, 0)) {
        this.stop();
        return;
      }

      position.add(delta);
      if (this.target.onKineticPan(delta.scale(-1))) {
        this.stop();
        return;
      }

      content.mozRequestAnimationFrame(callback);
    }).bind(this);

    content.mozRequestAnimationFrame(callback);
  }
};

function dump(a) {
  Cc["@mozilla.org/consoleservice;1"].getService(Ci.nsIConsoleService).logStringMessage(a);
}

const AsyncPanZoom = {
  init: function() {
    Services.obs.addObserver(this, "Viewport:Change", false);
    Services.obs.addObserver(this, "Viewport:ScreenSize", false);
    Services.obs.addObserver(this, "Gesture:SingleTap", false);
    Services.obs.addObserver(this, "Gesture:CancelTouch", false);
  },

  observe: function(aSubject, aTopic, aData) {
    dump("*************OBSERVE: " + aTopic);
    switch (aTopic) {
      case 'Viewport:ScreenSize':
        let newSize = JSON.parse(aData);
        this.screenWidth = aData.x;
        this.screenHeight = aData.y;
        break;

      case 'Viewport:Change':

        let aViewport = JSON.parse(aData);
        // Transform coordinates based on zoom
        let x = aViewport.x / aViewport.zoom;
        let y = aViewport.y / aViewport.zoom;

        // Set scroll position
        let win = content;
        win.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).setScrollPositionClampingScrollPortSize(
            this.screenWidth / aViewport.zoom, this.screenHeight / aViewport.zoom);
        win.scrollTo(x, y);
        this.userScrollPos.x = win.scrollX;
        this.userScrollPos.y = win.scrollY;
        this.setResolution(aViewport.zoom, false);

        if (aViewport.displayPort)
          this.setDisplayPort(aViewport.displayPort);
        break;
      case 'Gesture:CancelTouch':
        //this._cancelTapHighlight();
        break;
      case 'Gesture:SingleTap':
        let element = this._highlightElement;
        if (element) {
          try {
            let data = JSON.parse(aData);
            let isClickable = this.isElementClickable(element);

            var params = { movePoint: isClickable};
            this._sendMouseEvent("mousemove", element, data.x, data.y, params);
            this._sendMouseEvent("mousedown", element, data.x, data.y, params);
            this._sendMouseEvent("mouseup",   element, data.x, data.y, params);

            //if (isClickable)
            //  Haptic.performSimpleAction(Haptic.LongPress);
          } catch(e) {
            Cu.reportError(e);
          }
        }
        break;
    }
  },

  _sendMouseEvent: function(aName, aElement, aX, aY, aParams) {
    // the element can be out of the aX/aY point because of the touch radius
    // if outside, we gracefully move the touch point to the edge of the element
    if (!(aElement instanceof HTMLHtmlElement) && aParams.movePoint) {
      let isTouchClick = true;
      let rects = this.getContentClientRects(aElement);
      for (let i = 0; i < rects.length; i++) {
        let rect = rects[i];
        let inBounds =
          (aX> rect.left  && aX < (rect.left + rect.width)) &&
          (aY > rect.top && aY < (rect.top + rect.height));
        if (inBounds) {
          isTouchClick = false;
          break;
        }
      }

      if (isTouchClick) {
        let rect = rects[0];
        if (rect.width == 0 && rect.height == 0)
          return;

        aX = Math.min(rect.left + rect.width, Math.max(rect.left, aX));
        aY = Math.min(rect.top + rect.height, Math.max(rect.top,  aY));
      }
    }

    let window = aElement.ownerDocument.defaultView;
    try {
      let cwu = window.top.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
      cwu.sendMouseEventToWindow(aName, Math.round(aX), Math.round(aY), 0, 1, 0, true);
    } catch(e) {
      Cu.reportError(e);
    }
  },

  getContentClientRects: function(aElement) {
    let offset = { x: 0, y: 0 };

    let nativeRects = aElement.getClientRects();
    // step out of iframes and frames, offsetting scroll values
    for (let frame = aElement.ownerDocument.defaultView; frame.frameElement; frame = frame.parent) {
      // adjust client coordinates' origin to be top left of iframe viewport
      let rect = frame.frameElement.getBoundingClientRect();
      let left = frame.getComputedStyle(frame.frameElement, "").borderLeftWidth;
      let top = frame.getComputedStyle(frame.frameElement, "").borderTopWidth;
      offset.x += rect.left + parseInt(left);
      offset.y += rect.top + parseInt(top);
    }

    let result = [];
    for (let i = nativeRects.length - 1; i >= 0; i--) {
      let r = nativeRects[i];
      result.push({ left: r.left + offset.x,
                    top: r.top + offset.y,
                    width: r.width,
                    height: r.height
                  });
    }
    return result;
  },

  isElementClickable: function(aElement, aUnclickableCache, aAllowBodyListeners) {
    const selector = "a,:link,:visited,[role=button],button,input,select,textarea,label";

    let stopNode = null;
    if (!aAllowBodyListeners && aElement && aElement.ownerDocument)
      stopNode = aElement.ownerDocument.body;

    for (let elem = aElement; elem && elem != stopNode; elem = elem.parentNode) {
      if (aUnclickableCache && aUnclickableCache.indexOf(elem) != -1)
        continue;
      if (this._hasMouseListener(elem))
        return true;
      if (elem.mozMatchesSelector && elem.mozMatchesSelector(selector))
        return true;
      if (aUnclickableCache)
        aUnclickableCache.push(elem);
    }
    return false;
  },

  setResolution: function(aZoom, aForce) {
    // Set zoom level
    if (aForce || Math.abs(aZoom - this._zoom) >= 1e-6) {
      this._zoom = aZoom;
      if (BrowserApp.selectedTab == this) {
        let cwu = window.top.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
        this._drawZoom = aZoom;
        cwu.setResolution(aZoom, aZoom);
      }
    }
  },

  setViewport: function(aViewport) {
    // Transform coordinates based on zoom
    let x = aViewport.x / aViewport.zoom;
    let y = aViewport.y / aViewport.zoom;

    // Set scroll position
    let win = content;
    win.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils).setScrollPositionClampingScrollPortSize(
        this.screenWidth / aViewport.zoom, this.screenHeight / aViewport.zoom);
    win.scrollTo(x, y);
    this.userScrollPos.x = win.scrollX;
    this.userScrollPos.y = win.scrollY;
    this.setResolution(aViewport.zoom, false);

    if (aViewport.displayPort)
      this.setDisplayPort(aViewport.displayPort);
  },

  setDisplayPort: function(aDisplayPort) {
    let zoom = this._zoom;
    let resolution = aDisplayPort.resolution;
    if (zoom <= 0 || resolution <= 0)
      return;

    // "zoom" is the user-visible zoom of the "this" tab
    // "resolution" is the zoom at which we wish gecko to render "this" tab at
    // these two may be different if we are, for example, trying to render a
    // large area of the page at low resolution because the user is panning real
    // fast.
    // The gecko scroll position is in CSS pixels. The display port rect
    // values (aDisplayPort), however, are in CSS pixels multiplied by the desired
    // rendering resolution. Therefore care must be taken when doing math with
    // these sets of values, to ensure that they are normalized to the same coordinate
    // space first.

    let element = this.browser.contentDocument.documentElement;
    if (!element)
      return;

    // we should never be drawing background tabs at resolutions other than the user-
    // visible zoom. for foreground tabs, however, if we are drawing at some other
    // resolution, we need to set the resolution as specified.
    let cwu = window.top.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
    if (BrowserApp.selectedTab == this) {
      if (resolution != this._drawZoom) {
        this._drawZoom = resolution;
        cwu.setResolution(resolution, resolution);
      }
    } else if (resolution != zoom) {
      dump("Warning: setDisplayPort resolution did not match zoom for background tab!");
    }

    // Finally, we set the display port, taking care to convert everything into the CSS-pixel
    // coordinate space, because that is what the function accepts. Also we have to fudge the
    // displayport somewhat to make sure it gets through all the conversions gecko will do on it
    // without deforming too much. See https://bugzilla.mozilla.org/show_bug.cgi?id=737510#c10
    // for details on what these operations are.
    let geckoScrollX = content.scrollX;
    let geckoScrollY = content.scrollY;
    aDisplayPort = this._dirtiestHackEverToWorkAroundGeckoRounding(aDisplayPort, geckoScrollX, geckoScrollY);

    cwu = content.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
    cwu.setDisplayPortForElement((aDisplayPort.left / resolution) - geckoScrollX,
                                 (aDisplayPort.top / resolution) - geckoScrollY,
                                 (aDisplayPort.right - aDisplayPort.left) / resolution,
                                 (aDisplayPort.bottom - aDisplayPort.top) / resolution,
                                 element);
  },

  /*
   * Yes, this is ugly. But it's currently the safest way to account for the rounding errors that occur
   * when we pump the displayport coordinates through gecko and they pop out in the compositor.
   *
   * In general, the values are converted from page-relative device pixels to viewport-relative app units,
   * and then back to page-relative device pixels (now as ints). The first half of this is only slightly
   * lossy, but it's enough to throw off the numbers a little. Because of this, when gecko calls
   * ScaleToOutsidePixels to generate the final rect, the rect may get expanded more than it should,
   * ending up a pixel larger than it started off. This is undesirable in general, but specifically
   * bad for tiling, because it means we means we end up painting one line of pixels from a tile,
   * causing an otherwise unnecessary upload of the whole tile.
   *
   * In order to counteract the rounding error, this code simulates the conversions that will happen
   * to the display port, and calculates whether or not that final ScaleToOutsidePixels is actually
   * expanding the rect more than it should. If so, it determines how much rounding error was introduced
   * up until that point, and adjusts the original values to compensate for that rounding error.
   */
  _dirtiestHackEverToWorkAroundGeckoRounding: function(aDisplayPort, aGeckoScrollX, aGeckoScrollY) {
    const APP_UNITS_PER_CSS_PIXEL = 60.0;
    const EXTRA_FUDGE = 0.04;

    let resolution = aDisplayPort.resolution;

    // Some helper functions that simulate conversion processes in gecko

    function cssPixelsToAppUnits(aVal) {
      return Math.floor((aVal * APP_UNITS_PER_CSS_PIXEL) + 0.5);
    }

    function appUnitsToDevicePixels(aVal) {
      return aVal / APP_UNITS_PER_CSS_PIXEL * resolution;
    }

    function devicePixelsToAppUnits(aVal) {
      return cssPixelsToAppUnits(aVal / resolution);
    }

    // Stash our original (desired) displayport width and height away, we need it
    // later and we might modify the displayport in between.
    let originalWidth = aDisplayPort.right - aDisplayPort.left;
    let originalHeight = aDisplayPort.bottom - aDisplayPort.top;

    // This is the first conversion the displayport goes through, going from page-relative
    // device pixels to viewport-relative app units.
    let appUnitDisplayPort = {
      x: cssPixelsToAppUnits((aDisplayPort.left / resolution) - aGeckoScrollX),
      y: cssPixelsToAppUnits((aDisplayPort.top / resolution) - aGeckoScrollY),
      w: cssPixelsToAppUnits((aDisplayPort.right - aDisplayPort.left) / resolution),
      h: cssPixelsToAppUnits((aDisplayPort.bottom - aDisplayPort.top) / resolution)
    };

    // This is the translation gecko applies when converting back from viewport-relative
    // device pixels to page-relative device pixels.
    let geckoTransformX = -Math.floor((-aGeckoScrollX * resolution) + 0.5);
    let geckoTransformY = -Math.floor((-aGeckoScrollY * resolution) + 0.5);

    // The final "left" value as calculated in gecko is:
    //    left = geckoTransformX + Math.floor(appUnitsToDevicePixels(appUnitDisplayPort.x))
    // In a perfect world, this value would be identical to aDisplayPort.left, which is what
    // we started with. However, this may not be the case if the value being floored has accumulated
    // enough error to drop below what it should be.
    // For example, assume geckoTransformX is 0, and aDisplayPort.left is 4, but
    // appUnitsToDevicePixels(appUnitsToDevicePixels.x) comes out as 3.9 because of rounding error.
    // That's bad, because the -0.1 error has caused it to floor to 3 instead of 4. (If it had errored
    // the other way and come out as 4.1, there's no problem). In this example, we need to increase the
    // "left" value by some amount so that the 3.9 actually comes out as >= 4, and it gets floored into
    // the expected value of 4. The delta values calculated below calculate that error amount (e.g. -0.1).
    let errorLeft = (geckoTransformX + appUnitsToDevicePixels(appUnitDisplayPort.x)) - aDisplayPort.left;
    let errorTop = (geckoTransformY + appUnitsToDevicePixels(appUnitDisplayPort.y)) - aDisplayPort.top;

    // If the error was negative, that means it will floor incorrectly, so we need to bump up the
    // original aDisplayPort.left and/or aDisplayPort.top values. The amount we bump it up by is
    // the error amount (increased by a small fudge factor to ensure it's sufficient), converted
    // backwards through the conversion process.
    if (errorLeft < 0) {
      aDisplayPort.left += appUnitsToDevicePixels(devicePixelsToAppUnits(EXTRA_FUDGE - errorLeft));
      // After we modify the left value, we need to re-simulate some values to take that into account
      appUnitDisplayPort.x = cssPixelsToAppUnits((aDisplayPort.left / resolution) - aGeckoScrollX);
      appUnitDisplayPort.w = cssPixelsToAppUnits((aDisplayPort.right - aDisplayPort.left) / resolution);
    }
    if (errorTop < 0) {
      aDisplayPort.top += appUnitsToDevicePixels(devicePixelsToAppUnits(EXTRA_FUDGE - errorTop));
      // After we modify the top value, we need to re-simulate some values to take that into account
      appUnitDisplayPort.y = cssPixelsToAppUnits((aDisplayPort.top / resolution) - aGeckoScrollY);
      appUnitDisplayPort.h = cssPixelsToAppUnits((aDisplayPort.bottom - aDisplayPort.top) / resolution);
    }

    // At this point, the aDisplayPort.left and aDisplayPort.top values have been corrected to account
    // for the error in conversion such that they end up where we want them. Now we need to also do the
    // same for the right/bottom values so that the width/height end up where we want them.

    // This is the final conversion that the displayport goes through before gecko spits it back to
    // us. Note that the width/height calculates are of the form "ceil(transform(right)) - floor(transform(left))"
    let scaledOutDevicePixels = {
      x: Math.floor(appUnitsToDevicePixels(appUnitDisplayPort.x)),
      y: Math.floor(appUnitsToDevicePixels(appUnitDisplayPort.y)),
      w: Math.ceil(appUnitsToDevicePixels(appUnitDisplayPort.x + appUnitDisplayPort.w)) - Math.floor(appUnitsToDevicePixels(appUnitDisplayPort.x)),
      h: Math.ceil(appUnitsToDevicePixels(appUnitDisplayPort.y + appUnitDisplayPort.h)) - Math.floor(appUnitsToDevicePixels(appUnitDisplayPort.y))
    };

    // The final "width" value as calculated in gecko is scaledOutDevicePixels.w.
    // In a perfect world, this would equal originalWidth. However, things are not perfect, and as before,
    // we need to calculate how much rounding error has been introduced. In this case the rounding error is causing
    // the Math.ceil call above to ceiling to the wrong final value. For example, 4 gets converted 4.1 and gets
    // ceiling'd to 5; in this case the error is 0.1.
    let errorRight = (appUnitsToDevicePixels(appUnitDisplayPort.x + appUnitDisplayPort.w) - scaledOutDevicePixels.x) - originalWidth;
    let errorBottom = (appUnitsToDevicePixels(appUnitDisplayPort.y + appUnitDisplayPort.h) - scaledOutDevicePixels.y) - originalHeight;

    // If the error was positive, that means it will ceiling incorrectly, so we need to bump down the
    // original aDisplayPort.right and/or aDisplayPort.bottom. Again, we back-convert the error amount
    // with a small fudge factor to figure out how much to adjust the original values.
    if (errorRight > 0) aDisplayPort.right -= appUnitsToDevicePixels(devicePixelsToAppUnits(errorRight + EXTRA_FUDGE));
    if (errorBottom > 0) aDisplayPort.bottom -= appUnitsToDevicePixels(devicePixelsToAppUnits(errorBottom + EXTRA_FUDGE));

    // Et voila!
    return aDisplayPort;
  }
};

AsyncPanZoom.init();

setTimeout()
