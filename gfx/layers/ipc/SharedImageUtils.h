/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_SHAREDIMAGEUTILS_H
#define MOZILLA_LAYERS_SHAREDIMAGEUTILS_H

#include "mozilla/layers/ImageContainerChild.h"

namespace mozilla {
namespace layers {

template<typename Deallocator>
void DeallocSharedImageData(Deallocator* protocol, const SharedImage& aImage)
{
  if (aImage.type() == SharedImage::TYUVImage) {
    protocol->DeallocShmem(aImage.get_YUVImage().Ydata());
    protocol->DeallocShmem(aImage.get_YUVImage().Udata());
    protocol->DeallocShmem(aImage.get_YUVImage().Vdata());
  } else if (aImage.type() == SharedImage::TSurfaceDescriptor) {
    protocol->DeallocShmem(aImage.get_SurfaceDescriptor().get_Shmem());
  }
}

} // namespace
} // namespace

#endif

