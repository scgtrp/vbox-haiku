/* $Id$ */
/** @file
 * VBoxGuest - Guest Additions Driver, bits shared with the windows code.
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
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/string.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/version.h>
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
# include "revision-generated.h"
#endif


/**
 * Report the guest information to the host.
 *
 * @returns IPRT status code.
 * @param   enmOSType       The OS type to report.
 */
int VBoxGuestReportGuestInfo(VBOXOSTYPE enmOSType)
{
    /*
     * Important: VMMDev *awaits* a VMMDevReportGuestInfo or VMMDevReportGuestInfo2 message
     *            first in order to accept all other VMMDev messages! Otherwise you'd get
     *            a VERR_NOT_SUPPORTED error.
     */
    VMMDevReportGuestInfo2 *pReq = NULL;
    int rc = VbglGRAlloc((VMMDevRequestHeader **)&pReq, sizeof (VMMDevReportGuestInfo2), VMMDevReq_ReportGuestInfo2);
    Log(("VBoxGuestReportGuestInfo: VbglGRAlloc VMMDevReportGuestInfo2 completed with rc=%Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        pReq->guestInfo.additionsMajor = VBOX_VERSION_MAJOR;
        pReq->guestInfo.additionsMinor = VBOX_VERSION_MINOR;
        pReq->guestInfo.additionsBuild = VBOX_VERSION_BUILD;
        pReq->guestInfo.additionsRevision = VBOX_SVN_REV;
        pReq->guestInfo.additionsFeatures = 0; /* Not (never?) used. */
        RTStrCopy(pReq->guestInfo.szName, sizeof(pReq->guestInfo.szName), VBOX_VERSION_STRING);

        rc = VbglGRPerform(&pReq->header);
        Log(("VBoxGuestReportGuestInfo: VbglGRPerform VMMDevReportGuestInfo2 completed with rc=%Rrc\n", rc));
        if (rc == VERR_NOT_IMPLEMENTED) /* Compatibility with older hosts. */
            rc = VINF_SUCCESS;
        VbglGRFree(&pReq->header);
    }

    /*
     * VMMDevReportGuestInfo acts as a beacon and signals the host that all guest information is
     * now complete.  So always send this report last!
     */
    if (RT_SUCCESS(rc))
    {
        VMMDevReportGuestInfo *pReq3 = NULL;
        rc = VbglGRAlloc((VMMDevRequestHeader **)&pReq3, sizeof (VMMDevReportGuestInfo), VMMDevReq_ReportGuestInfo);
        Log(("VBoxGuestReportGuestInfo: VbglGRAlloc VMMDevReportGuestInfo completed with rc=%Rrc\n", rc));
        if (RT_SUCCESS(rc))
        {
            pReq3->guestInfo.interfaceVersion = VMMDEV_VERSION;
            pReq3->guestInfo.osType = enmOSType;

            rc = VbglGRPerform(&pReq3->header);
            Log(("VBoxGuestReportGuestInfo: VbglGRPerform VMMDevReportGuestInfo completed with rc=%Rrc\n", rc));
            VbglGRFree(&pReq3->header);
        }
    }

    return rc;
}


/**
 * Report the guest driver status to the host.
 *
 * @returns IPRT status code.
 * @param   fActive         Flag whether the driver is now active or not.
 */
int VBoxGuestReportDriverStatus(bool fActive)
{
    /*
     * Report guest status of the VBox driver to the host.
     */
    VMMDevReportGuestStatus *pReq2 = NULL;
    int rc = VbglGRAlloc((VMMDevRequestHeader **)&pReq2, sizeof(*pReq2), VMMDevReq_ReportGuestStatus);
    Log(("VBoxGuestReportDriverStatus: VbglGRAlloc VMMDevReportGuestStatus completed with rc=%Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        pReq2->guestStatus.facility = VBoxGuestStatusFacility_VBoxGuestDriver;
        pReq2->guestStatus.status = fActive ?
                                    VBoxGuestStatusCurrent_Active
                                  : VBoxGuestStatusCurrent_Inactive;
        pReq2->guestStatus.flags = 0;
        rc = VbglGRPerform(&pReq2->header);
        Log(("VBoxGuestReportDriverStatus: VbglGRPerform VMMDevReportGuestStatus completed with fActive=%d, rc=%Rrc\n",
             rc, fActive ? 1 : 0));
        if (rc == VERR_NOT_IMPLEMENTED) /* Compatibility with older hosts. */
            rc = VINF_SUCCESS;
        VbglGRFree(&pReq2->header);
    }

    return rc;
}
