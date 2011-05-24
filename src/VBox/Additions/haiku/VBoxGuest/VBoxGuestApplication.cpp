/** @file
 * VBoxGuest - Guest Additions Application
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <errno.h>
#define DEBUG 1
#include <Alert.h>
#include <Debug.h>
#include <Invoker.h>
#include <String.h>

#include "VBoxClipboard.h"
#include "VBoxGuestApplication.h"
#include "VBoxGuestDeskbarView.h"


int gVBoxDriverFD;

VBoxGuestApplication::VBoxGuestApplication(int driverFD)
	: BApplication(VBOX_GUEST_APP_SIG),
	fVBoxDriverFD(driverFD),
	fClipboardService(NULL),
	fAboutShowing(false)
{
	PRINT(("%s()\n", __FUNCTION__));
	
	
}

VBoxGuestApplication::~VBoxGuestApplication()
{
	PRINT(("%s()\n", __FUNCTION__));
}

void VBoxGuestApplication::ReadyToRun()
{
	status_t err;

	fClipboardService = new VBoxClipboardService();
	if (fClipboardService != NULL) {
		AddHandler(fClipboardService);
		fClipboardService->Connect();
	} else
		LogRel(("VBoxGuestApplication: Could not allocate VBoxClipboardService\n"));

	err = VBoxGuestDeskbarView::AddToDeskbar();
	PRINT(("%s: VBoxGuestDeskbarView::AddToDeskbar: 0x%08lx\n", __FUNCTION__, err));


}

void VBoxGuestApplication::AboutRequested()
{
	if (fAboutShowing)
		return;
	BString alertText;
	alertText << VBOX_PRODUCT << " \nGuest Additions ";
	alertText << VBOX_VERSION_MAJOR << "." << VBOX_VERSION_MINOR << "." << VBOX_VERSION_BUILD;
	alertText << "r" << VBOX_SVN_REV;

	BAlert *alert = new BAlert("About " VBOX_PRODUCT, alertText.String(), "Ok");
	fAboutShowing = true;
	alert->Go(new BInvoker(new BMessage('AbOk'), this));
}

bool VBoxGuestApplication::QuitRequested()
{
	VBoxGuestDeskbarView::RemoveFromDeskbar();
	fClipboardService->Disconnect();
	return BApplication::QuitRequested();
}


void VBoxGuestApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'AbOk':
			fAboutShowing = false;
			break;
		default:
			BApplication::MessageReceived(message);
	}
}

static int vboxOpenBaseDriver()
{
	int fd;
	PRINT(("%s()\n", __FUNCTION__));

	fd = open(VBOXGUEST_DEVICE_NAME, O_RDWR);
	if (fd < 0) {
		fd = errno;
        LogRel(("VBoxGuestApplication: Could not open VirtualBox Guest Additions driver! Please install first! Error: %s\n", strerror(fd)));
		return RTErrConvertFromErrno(fd);
	}
	gVBoxDriverFD = fd;
	return 0;
}

static int vboxCloseBaseDriver()
{
	status_t err = B_OK;
	PRINT(("%s()\n", __FUNCTION__));

	if (gVBoxDriverFD < 0)
		return B_OK;

	if (close(gVBoxDriverFD) < 0) {
		err = errno;
        LogRel(("VBoxGuestApplication: Could not close VirtualBox Guest Additions driver! Error: %s\n", strerror(err)));
	}
	return RTErrConvertFromErrno(err);
}



int main(int argc, const char **argv)
{
    int rc = RTR3Init();
    if (RT_SUCCESS(rc))
    {
        rc = VbglR3Init();
        if (RT_SUCCESS(rc))
            rc = vboxOpenBaseDriver();
    }

    if (RT_SUCCESS(rc))
    {
	    Log(("VBoxGuestApp: Init successful\n"));

		new VBoxGuestApplication(gVBoxDriverFD);
		be_app->Run();
		delete be_app;

    }

	if (RT_FAILURE(rc))
    {
        LogRel(("VBoxGuestApp: Error while starting, rc=%Rrc\n", rc));
    }
    LogRel(("VBoxGuestApp: Ended\n"));

	vboxCloseBaseDriver();

    VbglR3Term();

	return 0;
}
