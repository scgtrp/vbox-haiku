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

#ifndef __VBOXDISPLAY__H
#define __VBOXDISPLAY__H

#include <Handler.h>
#include <Screen.h>

class VBoxDisplayService : public BHandler {
public:
	VBoxDisplayService();
	virtual ~VBoxDisplayService();
	
	void		Start();

private:

static status_t	_ServiceThreadNub(void *_this);
	status_t	_ServiceThread();

	uint32_t	fClientId;
	thread_id	fServiceThreadID;
	bool		fExiting;
	BScreen		fScreen;
};


#endif /* __VBOXSERVICESHAREDCLIPLBOARD__H */
