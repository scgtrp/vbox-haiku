/** @file
 *
 * MS COM / XPCOM Abstraction Layer:
 * COM initialization / shutdown
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef __VBox_com_com_h__
#define __VBox_com_com_h__

#include "VBox/com/defs.h"

namespace com
{

/**
 *  Initializes the COM runtime.
 *  Must be called on every thread that uses COM, before any COM activity.
 *
 *  @return COM result code
 */
HRESULT Initialize();

/**
 *  Shuts down the COM runtime.
 *  Must be called on every thread before termination.
 *  No COM calls may be made after this method returns.
 */
void Shutdown();

/**
 *  Resolves a given interface ID to a string containing the interface name.
 *  If, for some reason, the given IID cannot be resolved to a name, a NULL
 *  string is returned. A non-NULL string returned by this funciton must be
 *  freed using SysFreeString().
 *
 *  @param aIID     ID of the interface to get a name for
 *  @param aName    Resolved interface name or @c NULL on error
 */
void GetInterfaceNameByIID (const GUID &aIID, BSTR *aName);

}; // namespace com

#endif // __VBox_com_com_h__

