/* $Id$ */
/** @file
 * VMM All Contexts.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm.h>
#include "VMMInternal.h"
#include <VBox/vm.h>
#include <VBox/param.h>


#ifndef IN_RING0
/**
 * Gets the bottom of the hypervisor stack - RC Ptr.
 *
 * (The returned address is not actually writable, only after it's decremented
 * by a push/ret/whatever does it become writable.)
 *
 * @returns bottom of the stack.
 * @param   pVM         The VM handle.
 */
RTRCPTR VMMGetStackRC(PVM pVM)
{
    return (RTRCPTR)pVM->vmm.s.pbEMTStackBottomRC;
}
#endif /* !IN_RING0 */


/**
 * Gets the current virtual CPU ID.
 *
 * @returns The CPU ID.
 * @param   pVM         Pointer to the shared VM handle.
 * @thread  EMT
 */
VMCPUID VMMGetCpuId(PVM pVM)
{
#ifdef VBOX_WITH_SMP_GUESTS
    /* Only emulation thread(s) allowed to ask for CPU id */
    VM_ASSERT_EMT(pVM);

# if defined(IN_RC)
    /* There is only one CPU if we're in GC. */
    return 0;

# elif defined(IN_RING3)
    return VMR3GetVMCPUId(pVM);

# else  /* IN_RING0 */
    /** @todo SMP: Get the real CPU ID and use a table in the VM structure to
     *  translate it. */
    return 0;
# endif /* IN_RING0 */

#else
    VM_ASSERT_EMT(pVM);
    return 0;
#endif
}


/**
 * Gets the VBOX_SVN_REV.
 *
 * This is just to avoid having to compile a bunch of big files
 * and requires less Makefile mess.
 *
 * @returns VBOX_SVN_REV.
 */
VMMDECL(uint32_t) VMMGetSvnRev(void)
{
    return VBOX_SVN_REV;
}

