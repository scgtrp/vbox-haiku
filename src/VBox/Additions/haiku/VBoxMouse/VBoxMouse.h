/** @file
 * VBoxMouse - Shared Clipboard
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

#ifndef __VBOXMOUSE__H
#define __VBOXMOUSE__H

#include <InputServerDevice.h>

extern "C" _EXPORT BInputServerDevice* instantiate_input_device();

class VBoxMouse : public BInputServerDevice {
public:
	VBoxMouse();
	virtual ~VBoxMouse();

	virtual status_t		InitCheck();
	virtual status_t		SystemShuttingDown();

	virtual status_t		Start(const char* device, void* cookie);
	virtual	status_t		Stop(const char* device, void* cookie);
	virtual status_t		Control(const char	*device,
									void		*cookie,
									uint32		code, 
									BMessage	*message);

private:

static status_t	_ServiceThreadNub(void *_this);
	status_t	_ServiceThread();

	int			fDriverFD;
	thread_id	fServiceThreadID;
	bool		fExiting;

};


#endif /* __VBOXMOUSE__H */
