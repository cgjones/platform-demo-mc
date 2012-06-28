/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_CAMERA_NSCAMERAPICTUREOPTIONS_H
#define DOM_CAMERA_NSCAMERAPICTUREOPTIONS_H


#include "nsIDOMCameraManager.h"


class nsCameraPictureOptions : public nsICameraPictureOptions
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICAMERAPICTUREOPTIONS

  nsCameraPictureOptions();

private:
  ~nsCameraPictureOptions();

protected:
  /* additional members */
};


#endif // DOM_CAMERA_NSCAMERAPICTUREOPTIONS_H
