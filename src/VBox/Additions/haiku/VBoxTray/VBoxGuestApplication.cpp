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

VBoxGuestApplication::VBoxGuestApplication()
	: BApplication(VBOX_GUEST_APP_SIG)
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

	err = VBoxGuestDeskbarView::AddToDeskbar();
	PRINT(("%s: VBoxGuestDeskbarView::AddToDeskbar: 0x%08lx\n", __FUNCTION__, err));
	
	exit(0);
}

int main(int argc, const char **argv)
{
	new VBoxGuestApplication();
	be_app->Run();
	delete be_app;
	
/*    int rc = RTR3Init();
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

    VbglR3Term();*/

	return 0;
}
