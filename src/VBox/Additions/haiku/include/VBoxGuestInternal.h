/** @file
 *
 * VBoxGuestInternal -- Private Haiku additions declarations
 *
 */

/*
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
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

#ifndef __VBoxGuestInternal_h__
#define __VBoxGuestInternal_h__


/** The MIME signature of the VBoxGuest application. */
#define VBOX_GUEST_APP_SIG		"application/x-vnd.Oracle-VBoxGuest"


/** The code used for messages sent by and to the system tray. */
#define VBOX_GUEST_CLIPBOARD_HOST_MSG_READ_DATA	'vbC2'
#define VBOX_GUEST_CLIPBOARD_HOST_MSG_FORMATS	'vbC3'

/** The code used for messages sent by and to the system tray. */
#define VBOX_GUEST_APP_ACTION	'vbox'


#endif /* __VBoxGuestInternal_h__ */
