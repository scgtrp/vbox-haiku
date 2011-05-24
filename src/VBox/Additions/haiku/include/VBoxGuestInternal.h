/** @file
 *
 * VBoxGuestInternal -- Private Haiku additions declarations
 *
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
