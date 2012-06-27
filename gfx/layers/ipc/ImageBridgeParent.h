/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PImageBridgeParent.h"

class MessageLoop;

namespace mozilla {
namespace layers {

class CompositorParent;
/**
 * ImageBridgeParent is the manager Protocol of ImageContainerParent.
 * It's purpose is mainly to setup the IPDL connection. Most of the
 * interesting stuff is in ImageContainerParent.
 */
class ImageBridgeParent : public PImageBridgeParent
{
public:

  ImageBridgeParent(MessageLoop* aLoop);
  ~ImageBridgeParent();

  // Overriden from PImageBridgeParent.
  PImageContainerParent* AllocPImageContainer(PRUint64* aID);
  // Overriden from PImageBridgeParent.
  bool DeallocPImageContainer(PImageContainerParent* toDealloc);

  MessageLoop * GetMessageLoop();

private:
  MessageLoop * mMessageLoop;
};

} // layers
} // mozilla

