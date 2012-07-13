/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositeEvent.h"

#include "mozilla/Services.h"

namespace mozilla {
namespace layers {

ViewportEvent::ViewportEvent(const nsAString& aType, const nsAString& aData)
  : mType(aType),
    mData(aData)
{

}

NS_IMETHODIMP ViewportEvent::Run() {
  nsCOMPtr<nsIObserverService> obsServ =
    mozilla::services::GetObserverService();

  const NS_ConvertUTF16toUTF8 topic(mType);
  const nsPromiseFlatString& data = PromiseFlatString(mData);

  obsServ->NotifyObservers(nsnull, topic.get(), data.get());

  return NS_OK;
}

GestureEvent::GestureEvent(const nsAString& aTopic, const nsAString& aData)
  : mTopic(aTopic),
    mData(aData)
{

}

NS_IMETHODIMP GestureEvent::Run() {
  nsCOMPtr<nsIObserverService> obsServ =
    mozilla::services::GetObserverService();

  const NS_ConvertUTF16toUTF8 topic(mTopic);
  const nsPromiseFlatString& data = PromiseFlatString(mData);

  obsServ->NotifyObservers(nsnull, topic.get(), data.get());

  return NS_OK;
}

}
}
