/* $Id$ */
/** @file
 * VBoxGuest - Guest Additions Driver.
 */

/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*
 * This file must be included _after_ all VBox headers. Its purpose is to rewrite calls
 * to functions in vboxguest so that they go through the lookup table in the vboxguest
 * module info structure.
 */

#ifndef ___VBoxGuest_haiku_mangling_h
#define ___VBoxGuest_haiku_mangling_h

#ifdef IN_VBOXGUEST
#define g_DevExt (g_VBoxGuest.devExt)
#define cUsers (g_VBoxGuest._cUsers)
#define sState (g_VBoxGuest._sState)
#else
#define g_DevExt (g_VBoxGuest->devExt)
#define cUsers (g_VBoxGuest->_cUsers)
#define sState (g_VBoxGuest->_sState)
#define RT_MANGLER(f) (g_VBoxGuest->_ ## f)

// technically not part of the runtime, but need to be exported like they are:
#define VBoxGuestCommonIOCtl RT_MANGLER(VBoxGuestCommonIOCtl)
#define VBoxGuestCloseSession RT_MANGLER(VBoxGuestCloseSession)
#define VBoxGuestCreateUserSession RT_MANGLER(VBoxGuestCreateUserSession)
#endif

#endif