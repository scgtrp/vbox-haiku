/** @file
 *
 * VBoxMouseFilter - Mouse input_server add-on
 *
 */

/*
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
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