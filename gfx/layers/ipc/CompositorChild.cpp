/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorChild.h"
#include "CompositorParent.h"
#include "LayerManagerOGL.h"
#include "mozilla/layers/ShadowLayersChild.h"

using mozilla::layers::ShadowLayersChild;

namespace mozilla {
namespace layers {

/*static*/ nsRefPtr<CompositorChild> CompositorChild::sCompositor;

CompositorChild::CompositorChild(LayerManager *aLayerManager)
  : mLayerManager(aLayerManager)
{
  MOZ_COUNT_CTOR(CompositorChild);
}

CompositorChild::~CompositorChild()
{
  MOZ_COUNT_DTOR(CompositorChild);
}

void
CompositorChild::Destroy()
{
  mLayerManager = NULL;
  size_t numChildren = ManagedPLayersChild().Length();
  NS_ABORT_IF_FALSE(0 == numChildren || 1 == numChildren,
                    "compositor must only have 0 or 1 layer forwarder");

  if (numChildren) {
    ShadowLayersChild* layers =
      static_cast<ShadowLayersChild*>(ManagedPLayersChild()[0]);
    layers->Destroy();
  }
  SendStop();
}

/*static*/ PCompositorChild*
CompositorChild::Create(Transport* aTransport, ProcessId aOtherProcess)
{
  printf_stderr("CompositorChild::Create\n");

  // There's only one compositor per child process.
  MOZ_ASSERT(!sCompositor);

  nsRefPtr<CompositorChild> cc = new CompositorChild(nsnull);
  ProcessHandle handle;
  if (!base::OpenProcessHandle(aOtherProcess, &handle)) {
    // We can't go on without a compositor.
    NS_RUNTIMEABORT("Couldn't OpenProcessHandle() to parent process.");
    return nsnull;
  }
  if (!cc->Open(aTransport, handle, XRE_GetIOMessageLoop(),
                AsyncChannel::Child)) {
    NS_RUNTIMEABORT("Couldn't Open() Compositor channel.");
    return nsnull;
  }
  sCompositor = cc;
  // This is a little scary but we promise to be good.  The return
  // value is just compared to null for success checking; the value
  // itself isn't used.  If that ever changes, we'll crash, hard.
  return reinterpret_cast<PCompositorChild*>(1);
}

PLayersChild*
CompositorChild::AllocPLayers(const LayersBackend &aBackend, const int64_t& aId, int* aMaxTextureSize)
{
  return new ShadowLayersChild();
}

bool
CompositorChild::DeallocPLayers(PLayersChild* actor)
{
  delete actor;
  return true;
}

} // namespace layers
} // namespace mozilla

