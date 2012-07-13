/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- /
/* vim: set shiftwidth=2 tabstop=2 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

'use strict';

dump('======================= async-pan-zoom.js ======================= \n');

let { classes: Cc, interfaces: Ci, utils: Cu }  = Components;
Cu.import('resource://gre/modules/XPCOMUtils.jsm');
Cu.import('resource://gre/modules/Services.jsm');
Cu.import('resource://gre/modules/Geometry.jsm');

function dump(a) {
  Cc["@mozilla.org/consoleservice;1"].getService(Ci.nsIConsoleService).logStringMessage(a);
}

var asyncPanZoom;
const AsyncPanZoom = {
  init: function() {
    asyncPanZoom = this;

    addMessageListener("Viewport:Change", function(data) {
      let aViewport = data.json;

      asyncPanZoom.screenWidth = aViewport.screenSize.width;
      asyncPanZoom.screenHeight = aViewport.screenSize.height;

      let x = aViewport.x;
      let y = aViewport.y;

      let win = content;
      let cwu = win.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
      cwu.setCSSViewport(asyncPanZoom.screenWidth, asyncPanZoom.screenHeight);

      // Set scroll position
      cwu.setScrollPositionClampingScrollPortSize(
          asyncPanZoom.screenWidth / aViewport.zoom, asyncPanZoom.screenHeight / aViewport.zoom);
      win.scrollTo(x, y);
      asyncPanZoom.userScrollPos = { x: win.scrollX, y: win.scrollY };
      asyncPanZoom.setResolution(aViewport.zoom, false);

      asyncPanZoom.setDisplayPort(aViewport.displayPort);
    });
  },

  setResolution: function(aZoom, aForce) {
    // Set zoom level
    if (aForce || Math.abs(aZoom - this._zoom) >= 1e-6) {
      this._zoom = aZoom;
      let cwu = window.top.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
      this._drawZoom = aZoom;
      cwu.setResolution(aZoom, aZoom);
    }
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

    if (!content.document)
      return;

    let element = content.document.documentElement;
    if (!element)
      return;

    let cwu = content.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
    if (resolution != this._drawZoom) {
      this._drawZoom = resolution;
      cwu.setResolution(resolution, resolution);
    }

    cwu = content.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDOMWindowUtils);
    cwu.setDisplayPortForElement(aDisplayPort.left,
                                 aDisplayPort.top,
                                 aDisplayPort.width,
                                 aDisplayPort.height,
                                 element);
  },
};

AsyncPanZoom.init();
