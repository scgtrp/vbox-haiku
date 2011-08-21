/** @file
 * VBoxGuestDeskbarView - Guest Additions Deskbar Tray View
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBOXGUESTTRAYVIEW__H
#define __VBOXGUESTTRAYVIEW__H

#include <Bitmap.h>
#include <View.h>

#include <iprt/initterm.h>
#include <iprt/string.h>

#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/VBoxGuest.h> /** @todo use the VbglR3 interface! */
#include <VBox/VBoxGuestLib.h>

#include <VBoxGuestInternal.h>
#include "VBoxClipboard.h"
#include "VBoxDisplay.h"

#define REMOVE_FROM_DESKBAR_MSG 'vbqr'

class VBoxGuestDeskbarView : public BView {
public:
			VBoxGuestDeskbarView();
			VBoxGuestDeskbarView(BMessage *archive);
	virtual ~VBoxGuestDeskbarView();
	static  BArchivable	*Instantiate(BMessage *data);
	virtual	status_t	Archive(BMessage *data, bool deep = true) const;

			void		Draw(BRect rect);
			void		AttachedToWindow();
			void		DetachedFromWindow();

	virtual	void		MouseDown(BPoint point);
	virtual void		MessageReceived(BMessage* message);

	static status_t		AddToDeskbar(bool force=true);
	static status_t		RemoveFromDeskbar();
	
private:
	status_t			_Init(BMessage *archive=NULL);
	BBitmap				*fIcon;
	
	VBoxClipboardService *fClipboardService;
	VBoxDisplayService *fDisplayService;
};

#endif /* __VBOXGUESTTRAYVIEW__H */
