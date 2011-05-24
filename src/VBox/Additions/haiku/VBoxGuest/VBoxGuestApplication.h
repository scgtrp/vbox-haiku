/** @file
 * VBoxGuestApplication - Guest Additions Application
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

#ifndef __VBOXGUESTAPPLICATION__H
#define __VBOXGUESTAPPLICATION__H

#include <Application.h>

#include <iprt/initterm.h>
#include <iprt/string.h>

#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/VBoxGuest.h> /** @todo use the VbglR3 interface! */
#include <VBox/VBoxGuestLib.h>

#include <VBoxGuestInternal.h>

class VBoxClipboardService;

class VBoxGuestApplication : public BApplication {
public:
	VBoxGuestApplication(int driverFD);
	virtual ~VBoxGuestApplication();

	virtual void				ReadyToRun();
	virtual	void				AboutRequested();
	virtual bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

	int		VBoxDriverFD() const { return fVBoxDriverFD; };

private:

	int		fVBoxDriverFD;
	VBoxClipboardService	*fClipboardService;
	bool	fAboutShowing;
};


inline int VBoxGuestGetDriverFD(void)
{
	VBoxGuestApplication *app;
	app = dynamic_cast<VBoxGuestApplication *>(be_app);
	if (app)
		return app->VBoxDriverFD();
	return -1;
}

#endif /* __VBOXGUESTAPPLICATION__H */
