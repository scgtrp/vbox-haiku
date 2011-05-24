/* $Id: semevent-r0drv-haiku.c 33376 2010-10-24 12:55:23Z vboxsync $ */
/** @file
 * IPRT - Single Release Event Semaphores, Ring-0 Driver, Haiku.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-haiku-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>

#include "internal/magics.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Haiku event semaphore.
 */
typedef struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t volatile   u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** The semaphore id. */
    sem_id              fSem;
} RTSEMEVENTINTERNAL, *PRTSEMEVENTINTERNAL;


RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertCompile(sizeof(RTSEMEVENTINTERNAL) > sizeof(void *));
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));
    AssertPtrReturn(phEventSem, VERR_INVALID_POINTER);

    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic  = RTSEMEVENT_MAGIC;
    pThis->cRefs     = 1;
    pThis->fSem      = create_sem(0, "IPRT Semaphore Event");
    if (pThis->fSem < B_OK) {
        RTMemFree(pThis);
        return VERR_TOO_MANY_SEMAPHORES;
    }
    set_sem_owner(pThis->fSem, B_SYSTEM_TEAM);

    *phEventSem = pThis;
    return VINF_SUCCESS;
}


/**
 * Retains a reference to the event semaphore.
 *
 * @param   pThis       The event semaphore.
 */
DECLINLINE(void) rtR0SemEventHkuRetain(PRTSEMEVENTINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < 100000); NOREF(cRefs);
}


/**
 * Releases a reference to the event semaphore.
 *
 * @param   pThis       The event semaphore.
 */
DECLINLINE(void) rtR0SemEventHkuRelease(PRTSEMEVENTINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
        RTMemFree(pThis);
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs > 0);

    /*
     * Invalidate it and delete the semaphore to unblock everyone.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENT_MAGIC);
   	delete_sem(pThis->fSem);
   	pThis->fSem = -1;
    rtR0SemEventHkuRelease(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    rtR0SemEventHkuRetain(pThis);

    /*
     * Signal the event object.
     * We must use B_DO_NOT_RESCHEDULE since we are being used from an irq handler.
     */
    release_sem_etc(pThis->fSem, 1, B_DO_NOT_RESCHEDULE);
    rtR0SemEventHkuRelease(pThis);
    return VINF_SUCCESS;
}

/**
 * Worker for RTSemEventWaitEx and RTSemEventWaitExDebug.
 *
 * @returns VBox status code.
 * @param   pThis           The event semaphore.
 * @param   fFlags          See RTSemEventWaitEx.
 * @param   uTimeout        See RTSemEventWaitEx.
 * @param   pSrcPos         The source code position of the wait.
 */
static int rtR0SemEventWait(PRTSEMEVENTINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                            PCRTLOCKVALSRCPOS pSrcPos)
{
	status_t  status;
    int       rc;
    int32     flags = 0;
    bigtime_t timeout; /* in microseconds */

    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

	if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
		timeout = B_INFINITE_TIMEOUT;
	else {
		if (fFlags & RTSEMWAIT_FLAGS_NANOSECS)
			timeout = uTimeout / 1000;
		else if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
			timeout = uTimeout * 1000;
		else
			return VERR_INVALID_PARAMETER;

		if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
			flags |= B_RELATIVE_TIMEOUT;
		else if (fFlags & RTSEMWAIT_FLAGS_ABSOLUTE)
			flags |= B_ABSOLUTE_TIMEOUT;
		else
			return VERR_INVALID_PARAMETER;
	}

	if (fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE)
		flags |= B_CAN_INTERRUPT;
	// likely not:
	//else
	//	flags |= B_KILL_CAN_INTERRUPT;

    rtR0SemEventHkuRetain(pThis);

	status = acquire_sem_etc(pThis->fSem, 1, flags, timeout);

	switch (status) {
		case B_OK:
			rc = VINF_SUCCESS;
			break;
		case B_BAD_SEM_ID:
			rc = VERR_SEM_DESTROYED;
			break;
		case B_INTERRUPTED:
			rc = VERR_INTERRUPTED;
			break;
		case B_WOULD_BLOCK:
			// FALLTHROUGH ?
		case B_TIMED_OUT:
			rc = VERR_TIMEOUT;
			break;
		default:
			rc = RTErrConvertFromHaikuKernReturn(status);
			break;
	}

    rtR0SemEventHkuRelease(pThis);
    return rc;
}


RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventWait(hEventSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventWait(hEventSem, fFlags, uTimeout, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemEventWaitEx);


RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventWait(hEventSem, fFlags, uTimeout, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemEventWaitExDebug);


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
	/* At least that's what the API supports. */
    return 1000;
}

