/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CameraPictureOptions.h"


NS_IMPL_ISUPPORTS1(nsCameraPictureOptions, nsICameraPictureOptions)

nsCameraPictureOptions::nsCameraPictureOptions()
{
  /* member initializers and constructor code */
}

nsCameraPictureOptions::~nsCameraPictureOptions()
{
  /* destructor code */
}

/* attribute jsval pictureSize; */
NS_IMETHODIMP nsCameraPictureOptions::GetPictureSize(JS::Value *aPictureSize)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}
NS_IMETHODIMP nsCameraPictureOptions::SetPictureSize(const JS::Value & aPictureSize)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* attribute DOMString fileFormat; */
NS_IMETHODIMP nsCameraPictureOptions::GetFileFormat(nsAString & aFileFormat)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}
NS_IMETHODIMP nsCameraPictureOptions::SetFileFormat(const nsAString & aFileFormat)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* attribute long rotation; */
NS_IMETHODIMP nsCameraPictureOptions::GetRotation(PRInt32 *aRotation)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}
NS_IMETHODIMP nsCameraPictureOptions::SetRotation(PRInt32 aRotation)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* attribute jsval position; */
NS_IMETHODIMP nsCameraPictureOptions::GetPosition(JS::Value *aPosition)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}
NS_IMETHODIMP nsCameraPictureOptions::SetPosition(const JS::Value & aPosition)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}
