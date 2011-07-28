/** @file
 *
 * VBoxMouseFilter - Mouse input_server add-on
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <Clipboard.h>
#include <Debug.h>
#include <Message.h>
#include <String.h>

#include "VBoxMouseFilter.h"
#include <VBox/VBoxGuest.h> /** @todo use the VbglR3 interface! */
#include <VBox/VBoxGuestLib.h>
#include <VBoxGuestInternal.h>
#include <VBox/VMMDev.h>
#include <VBox/log.h>
#include <iprt/err.h>

// TODO can this be merged with VBoxMouse?

BInputServerFilter*
instantiate_input_filter()
{
	return new VBoxMouseFilter();
}

VBoxMouseFilter::VBoxMouseFilter()
	: BInputServerFilter(),
	fDriverFD(-1),
	fServiceThreadID(-1),
	fExiting(false),
	fCurrentButtons(0)
{
}

VBoxMouseFilter::~VBoxMouseFilter()
{
}

filter_result VBoxMouseFilter::Filter(BMessage* message, BList* outList)
{
	switch(message->what) {
		case B_MOUSE_UP:
		case B_MOUSE_DOWN:
		{
			printf("click|release\n");
			message->FindInt32("buttons", &fCurrentButtons);
		}
		case B_MOUSE_MOVED:
		{
			printf("mouse moved\n");
			message->ReplaceInt32("buttons", fCurrentButtons);
		}
	}
	
	return B_DISPATCH_MESSAGE;
}