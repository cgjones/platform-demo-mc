/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/ImageContainerParent.h"
#include "mozilla/layers/CompositorParent.h"

#include "base/thread.h"
#include "nsTArray.h"

namespace mozilla {
namespace layers {


ImageBridgeParent::ImageBridgeParent(MessageLoop* aLoop)
: mMessageLoop(aLoop)
{
  ImageContainerParent::CreateSharedImageMap();
}

ImageBridgeParent::~ImageBridgeParent()
{
  ImageContainerParent::DestroySharedImageMap();
}

namespace {
  PRUint64 GenImageID() {
    static PRUint64 sNextImageID = 1;
    ++sNextImageID;
    while ((sNextImageID == 0) || 
           ImageContainerParent::IsExistingID(sNextImageID)) {
      ++sNextImageID;
    }
    return sNextImageID;
  }
}

PImageContainerParent* ImageBridgeParent::AllocPImageContainer(PRUint64* aID)
{
  PRUint64 id = GenImageID();
  *aID = id;
  return new ImageContainerParent(this, id);
}

bool ImageBridgeParent::DeallocPImageContainer(PImageContainerParent* toDealloc)
{
  delete toDealloc;
  return true;
}


MessageLoop * ImageBridgeParent::GetMessageLoop() {
  return mMessageLoop;
}


} // layers
} // mozilla

