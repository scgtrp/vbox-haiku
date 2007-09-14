/* $Id$ */
/** @file
 *
 * VBox host drivers - Ring-0 support drivers - Solaris host:
 * Solaris implementations for support library
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP
#include <VBox/types.h>
#include <VBox/sup.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include "SUPLibInternal.h"
#include "SUPDRVIOC.h"

#include <sys/fcntl.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define DEVICE_NAME     "/devices/pseudo/vboxdrv@0:0"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Handle to the open device. */
static int              g_hDevice = -1;


int suplibOsInit(size_t cbReserve)
{
    /*
     * Check if already initialized.
     */
    if (g_hDevice >= 0)
        return VINF_SUCCESS;

    /*
     * Try to open the device.
     */
    g_hDevice = open(DEVICE_NAME, O_RDWR, 0);
    if (g_hDevice < 0)
    {
        int rc = errno;
        LogRel(("Failed to open \"%s\", errno=%d\n", rc));
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Avoid unused parameter warning
    */
    NOREF(cbReserve);

    return VINF_SUCCESS;
}


int suplibOsTerm(void)
{
    /*
     * Check if we're initialized
     */
    if (g_hDevice >= 0)
    {
        if (close(g_hDevice))
            AssertFailed();
        g_hDevice = -1;
    }

    return VINF_SUCCESS;
}


int suplibOsInstall(void)
{
    return VERR_NOT_IMPLEMENTED;
}

int suplibOsUninstall(void)
{
    return VERR_NOT_IMPLEMENTED;
}


int suplibOsIOCtl(uintptr_t uFunction, void *pvReq, size_t cbReq)
{
    AssertMsg(g_hDevice != -1, ("SUPLIB not initiated successfully!\n"));

    /*
     * Issue device iocontrol.
     */
    if (RT_LIKELY(ioctl(g_hDevice, uFunction, &pvReq) >= 0))
        return VINF_SUCCESS;

    /* This is the reverse operation of the one found in SUPDrv-solaris.c */
    switch (errno)
    {
        case EACCES: return VERR_GENERAL_FAILURE;
        case EINVAL: return VERR_INVALID_PARAMETER;
        case EILSEQ: return VERR_INVALID_MAGIC;
        case ENOSYS: return VERR_VERSION_MISMATCH;
        case ENXIO:  return VERR_INVALID_HANDLE;
        case EFAULT: return VERR_INVALID_POINTER;
        case ENOLCK: return VERR_LOCK_FAILED;
        case EEXIST: return VERR_ALREADY_LOADED;
        case EPERM:  return VERR_PERMISSION_DENIED;
    }

    return RTErrConvertFromErrno(errno);
}


#ifdef VBOX_WITHOUT_IDT_PATCHING
int suplibOSIOCtlFast(uintptr_t uFunction)
{
    int rc = ioctl(g_hDevice, uFunction, NULL);
    if (rc == -1)
        rc = errno;
    return rc;
}
#endif


int suplibOsPageAlloc(size_t cPages, void **ppvPages)
{
    *ppvPages = RTMemPageAllocZ(cPages << PAGE_SHIFT);
    if (*ppvPages)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


int suplibOsPageFree(void *pvPages, size_t /* cPages */)
{
    RTMemPageFree(pvPages);
    return VINF_SUCCESS;
}

