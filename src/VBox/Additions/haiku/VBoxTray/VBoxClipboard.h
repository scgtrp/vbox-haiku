/** @file
 * VBoxClipboard - Shared Clipboard
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

#ifndef __VBOXSERVICESHAREDCLIPLBOARD__H
#define __VBOXSERVICESHAREDCLIPLBOARD__H

#include <Handler.h>

class VBoxClipboardService : public BHandler {
public:
	VBoxClipboardService();
	virtual ~VBoxClipboardService();

	virtual status_t	Connect();
	virtual status_t	Disconnect();

	virtual	void			MessageReceived(BMessage* message);

private:

static status_t	_ServiceThreadNub(void *_this);
	status_t	_ServiceThread();

	void		*_VBoxReadHostClipboard(uint32_t format, uint32_t *pcb);

	uint32_t	fClientId;
	thread_id	fServiceThreadID;
	bool		fExiting;

};


#endif /* __VBOXSERVICESHAREDCLIPLBOARD__H */
