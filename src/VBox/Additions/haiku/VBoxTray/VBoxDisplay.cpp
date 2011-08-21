/** @file
 *
 * VBoxDisplay - Display resizing
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <DataIO.h>
#include <Message.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <String.h>

#include "VBoxGuestApplication.h"
#include "VBoxDisplay.h"
#include <VBoxGuestInternal.h>
#include "../VBoxVideo/common/VBoxVideo_common.h"

#include <iprt/mem.h>
#include <VBox/log.h>

#undef Log
#define Log(x) printf x
#undef LogRel
#define LogRel(x) printf x
#undef LogRelFlowFunc
#define LogRelFlowFunc(x) printf x

VBoxDisplayService::VBoxDisplayService()
	: BHandler("VBoxDisplayService"),
	fClientId(-1),
	fServiceThreadID(-1),
	fExiting(false),
	fScreen(B_MAIN_SCREEN_ID)
{
}

VBoxDisplayService::~VBoxDisplayService()
{
}

void VBoxDisplayService::Start()
{
	status_t err;
	err = fServiceThreadID = spawn_thread(_ServiceThreadNub,
		"VBoxDisplayService", B_NORMAL_PRIORITY, this);

	if (err >= B_OK) {
		resume_thread(fServiceThreadID);
	} else
		LogRel(("VBoxDisplayService: Error starting service thread: %s\n", strerror(err)));

}

void VBoxDisplayService::MessageReceived(BMessage* message)
{
	if (message->what == B_QUIT_REQUESTED)
		fExiting = true;
	else
		BHandler::MessageReceived(message);
}

status_t VBoxDisplayService::_ServiceThreadNub(void *_this)
{
	VBoxDisplayService *service = (VBoxDisplayService *)_this;
	return service->_ServiceThread();
}

status_t VBoxDisplayService::_ServiceThread()
{
    printf("VBoxDisplayService::%s()\n", __FUNCTION__);

    VbglR3CtlFilterMask(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, 0);
	VbglR3SetGuestCaps(VMMDEV_GUEST_SUPPORTS_GRAPHICS, 0);
	
    for (;;) {
		uint32_t events;
		
		int rc = VbglR3WaitEvent(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, 5000, &events);
		if (rc == -6) // timed out?
			continue;
		
		if (RT_SUCCESS(rc)) {
			uint32_t cx, cy, cBits, iDisplay;
            int rc2 = VbglR3GetDisplayChangeRequest(&cx, &cy, &cBits, &iDisplay, true);
			printf("rc2=%d screen %d size changed (%d, %d, %d)\n", rc2, iDisplay, cx, cy, cBits);
			
			if (RT_SUCCESS(rc2)) {
				display_mode mode;
				fScreen.GetMode(&mode);
				if (cBits == 0) {
					cBits = get_depth_for_color_space(mode.space);
				}
				mode.timing.h_display = cx;
				mode.timing.v_display = cy;
				mode.space = get_color_space_for_depth(cBits);
				mode.virtual_width = cx;
				mode.virtual_height = cy;
				
				/*= {
					{0, cx, 0, 0, cBits * cx / 8, cy, 0, 0, cBits * cy / 8, 0},
					get_color_space_for_depth(cBits),
					cx, cy, 0, 0, 0
				};*/
				
				fScreen.SetMode(&mode, false);
			}
        }
        else
        	fExiting = true;

        LogRelFlow(("processed host event rc = %d\n", rc));

        if (fExiting)
        	break;
    }
	return 0;
}