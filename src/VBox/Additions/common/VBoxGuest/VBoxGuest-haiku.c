/* $Id: VBoxGuest-haiku.c 34521 2010-11-30 14:35:40Z vboxsync $ */
/** @file
 * VirtualBox Guest Additions Driver for Haiku.
 */

/*
 * Copyright (C) 2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define LOG_GROUP LOG_GROUP_SUP_DRV
//#undef LOG_DISABLED
//#define LOG_ENABLED
//#define LOG_ENABLE_FLOW
//#define DO_LOG
#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <OS.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <PCI.h>

#include "VBoxGuestInternal.h"
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/process.h>
#include <iprt/mem.h>
#include <iprt/asm.h>

/** The module name. */
#define DRIVER_NAME    "vboxguest"

/** The published device name. */
#define DEVICE_NAME    "misc/vboxguest"

struct VBoxGuestDeviceState
{
    /** Resource ID of the I/O port */
    int                iIOPortResId;
    /** Pointer to the I/O port resource. */
//    struct resource   *pIOPortRes;
    /** Start address of the IO Port. */
    uint16_t           uIOPortBase;
    /** Resource ID of the MMIO area */
    area_id            iVMMDevMemAreaId;
    /** Pointer to the MMIO resource. */
//    struct resource   *pVMMDevMemRes;
    /** Handle of the MMIO resource. */
//    bus_space_handle_t VMMDevMemHandle;
    /** Size of the memory area. */
    size_t         VMMDevMemSize;
    /** Mapping of the register space */
    void              *pMMIOBase;
    /** IRQ number */
    int                iIrqResId;
    /** IRQ resource handle. */
//    struct resource   *pIrqRes;
    /** Pointer to the IRQ handler. */
//    void              *pfnIrqHandler;
    /** VMMDev version */
    uint32_t           u32Version;

    /** The (only) select data we wait on. */
    //XXX: should leave in pSession ?
    uint8_t						selectEvent;
    uint32_t                    selectRef;
    void                    	*selectSync;
};

static struct VBoxGuestDeviceState sState;

/*
 * Character device file handlers.
 */
static status_t VBoxGuestHaikuOpen(const char *name, uint32 flags, void **cookie);
static status_t VBoxGuestHaikuClose(void *cookie);
static status_t VBoxGuestHaikuFree(void *cookie);
static status_t VBoxGuestHaikuIOCtl(void *cookie, uint32 op, void *data, size_t len);
static status_t VBoxGuestHaikuSelect (void *cookie, uint8 event, uint32 ref, selectsync *sync);
static status_t VBoxGuestHaikuDeselect (void *cookie, uint8 event, selectsync *sync);
static status_t VBoxGuestHaikuWrite (void *cookie, off_t position, const void *data, size_t *numBytes);
static status_t VBoxGuestHaikuRead (void *cookie, off_t position, void *data, size_t *numBytes);

/*
 * IRQ related functions.
 */
static void  VBoxGuestHaikuRemoveIRQ(void *pvState);
static int   VBoxGuestHaikuAddIRQ(void *pvState);
static int32 VBoxGuestHaikuISR(void *pvState);

/*
 * Available functions for kernel drivers.
 */
DECLVBGL(int)    VBoxGuestHaikuServiceCall(void *pvSession, unsigned uCmd, void *pvData, size_t cbData, size_t *pcbDataReturned);
DECLVBGL(void *) VBoxGuestHaikuServiceOpen(uint32_t *pu32Version);
DECLVBGL(int)    VBoxGuestHaikuServiceClose(void *pvSession);

/*
 * Device node entry points.
 */
static device_hooks    g_VBoxGuestHaikuDeviceHooks =
{
    VBoxGuestHaikuOpen,
    VBoxGuestHaikuClose,
    VBoxGuestHaikuFree,
    VBoxGuestHaikuIOCtl,
    VBoxGuestHaikuRead,
    VBoxGuestHaikuWrite,
    VBoxGuestHaikuSelect,
    VBoxGuestHaikuDeselect,
};

int32	api_version = B_CUR_DRIVER_API_VERSION;

/** Device extention & session data association structure. */
static VBOXGUESTDEVEXT      g_DevExt;
/** List of cloned device. Managed by the kernel. */
//static struct clonedevs    *g_pVBoxGuestHaikuClones;
/** The dev_clone event handler tag. */
//static eventhandler_tag     g_VBoxGuestHaikuEHTag;
/** Reference counter */
static volatile uint32_t      cUsers;
/** selinfo structure used for polling. */
//static struct selinfo       g_SelInfo;
/** PCI Bus Manager Module */
static pci_module_info *gPCI;

#if 0
/**
 * DEVFS event handler.
 */
static void VBoxGuestHaikuClone(void *pvArg, struct ucred *pCred, char *pszName, int cchName, struct cdev **ppDev)
{
    int iUnit;
    int rc;

    Log(("VBoxGuestHaikuClone: pszName=%s ppDev=%p\n", pszName, ppDev));

    /*
     * One device node per user, si_drv1 points to the session.
     * /dev/vboxguest<N> where N = {0...255}.
     */
    if (!ppDev)
        return;
    if (strcmp(pszName, "vboxguest") == 0)
        iUnit =  -1;
    else if (dev_stdclone(pszName, NULL, "vboxguest", &iUnit) != 1)
        return;
    if (iUnit >= 256)
    {
        Log(("VBoxGuestHaikuClone: iUnit=%d >= 256 - rejected\n", iUnit));
        return;
    }

    Log(("VBoxGuestHaikuClone: pszName=%s iUnit=%d\n", pszName, iUnit));

    rc = clone_create(&g_pVBoxGuestHaikuClones, &g_VBoxGuestHaikuDeviceHooks, &iUnit, ppDev, 0);
    Log(("VBoxGuestHaikuClone: clone_create -> %d; iUnit=%d\n", rc, iUnit));
    if (rc)
    {
        *ppDev = make_dev(&g_VBoxGuestHaikuDeviceHooks,
                          iUnit,
                          UID_ROOT,
                          GID_WHEEL,
                          0644,
                          "vboxguest%d", iUnit);
        if (*ppDev)
        {
            dev_ref(*ppDev);
            (*ppDev)->si_flags |= SI_CHEAPCLONE;
            Log(("VBoxGuestHaikuClone: Created *ppDev=%p iUnit=%d si_drv1=%p si_drv2=%p\n",
                     *ppDev, iUnit, (*ppDev)->si_drv1, (*ppDev)->si_drv2));
            (*ppDev)->si_drv1 = (*ppDev)->si_drv2 = NULL;
        }
        else
            Log(("VBoxGuestHaikuClone: make_dev iUnit=%d failed\n", iUnit));
    }
    else
        Log(("VBoxGuestHaikuClone: Existing *ppDev=%p iUnit=%d si_drv1=%p si_drv2=%p\n",
             *ppDev, iUnit, (*ppDev)->si_drv1, (*ppDev)->si_drv2));
}
#endif

/**
 * File open handler
 *
 */
static status_t VBoxGuestHaikuOpen(const char *name, uint32 flags, void **cookie)
{
    int                 rc;
    PVBOXGUESTSESSION   pSession;

    LogFlow((DRIVER_NAME ":VBoxGuestHaikuOpen\n"));

    /*
     * Create a new session.
     */
    rc = VBoxGuestCreateUserSession(&g_DevExt, &pSession);
    if (RT_SUCCESS(rc))
    {
        Log((DRIVER_NAME ":VBoxGuestHaikuOpen success: g_DevExt=%p pSession=%p rc=%d pid=%d\n", &g_DevExt, pSession, rc, (int)RTProcSelf()));
        ASMAtomicIncU32(&cUsers);
        *cookie = pSession;
        return 0;
    }

    LogRel((DRIVER_NAME ":VBoxGuestHaikuOpen: failed. rc=%d\n", rc));
    return RTErrConvertToErrno(rc);
}

/**
 * File close handler
 *
 */
static status_t VBoxGuestHaikuClose(void *cookie)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
	RTSPINLOCKTMP tmp;
    Log(("VBoxGuestHaikuClose: pSession=%p\n", pSession));

	RTSpinlockAcquireNoInts(g_DevExt.SessionSpinlock, &tmp);
	
	//XXX: we don't know if it belongs to this session !
    if (sState.selectSync) {
    	//dprintf(DRIVER_NAME "close: unblocking select %p %x\n", sState.selectSync, sState.selectEvent);
		notify_select_event(sState.selectSync, sState.selectEvent);
		sState.selectEvent = (uint8_t)0;
		sState.selectRef = (uint32_t)0;
		sState.selectSync = (void *)NULL;
	}
	
	RTSpinlockReleaseNoInts(g_DevExt.SessionSpinlock, &tmp);

    return 0;
}

/**
 * File free handler
 *
 */
static status_t VBoxGuestHaikuFree(void *cookie)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
    Log(("VBoxGuestHaikuFree: pSession=%p\n", pSession));

    /*
     * Close the session if it's still hanging on to the device...
     */
    if (VALID_PTR(pSession))
    {
        VBoxGuestCloseSession(&g_DevExt, pSession);
        ASMAtomicDecU32(&cUsers);
    }
    else
        Log(("VBoxGuestHaikuFree: si_drv1=%p!\n", pSession));
    return 0;
}

/**
 * IOCTL handler
 *
 */
static status_t VBoxGuestHaikuIOCtl(void *cookie, uint32 op, void *data, size_t len)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
    //Log(("VBoxGuestHaikuFree: pSession=%p\n", pSession));
    //LogFlow((DRIVER_NAME ":VBoxGuestHaikuIOCtl(, 0x%08x, %p, %d)\n", op, data, len));
    Log((DRIVER_NAME ":VBoxGuestHaikuIOCtl(, 0x%08x, %p, %d)\n", op, data, len));

    int rc = 0;

    /*
     * Validate the input.
     */
    if (RT_UNLIKELY(!VALID_PTR(pSession)))
        return EINVAL;

    /*
     * Validate the request wrapper.
     */
#if 0
    if (IOCPARM_LEN(ulCmd) != sizeof(VBGLBIGREQ))
    {
        Log((DRIVER_NAME ": VBoxGuestHaikuIOCtl: bad request %lu size=%lu expected=%d\n", ulCmd, IOCPARM_LEN(ulCmd), sizeof(VBGLBIGREQ)));
        return ENOTTY;
    }
#endif

    if (RT_UNLIKELY(len > _1M*16))
    {
        dprintf(DRIVER_NAME ": VBoxGuestHaikuIOCtl: bad size %#x; pArg=%p Cmd=%lu.\n", len, data, op);
        return EINVAL;
    }

    /*
     * Read the request.
     */
    void *pvBuf = NULL;
    if (RT_LIKELY(len > 0))
    {
        pvBuf = RTMemTmpAlloc(len);
        if (RT_UNLIKELY(!pvBuf))
        {
            LogRel((DRIVER_NAME ":VBoxGuestHaikuIOCtl: RTMemTmpAlloc failed to alloc %d bytes.\n", len));
            return ENOMEM;
        }

        rc = user_memcpy(pvBuf, data, len);
        if (RT_UNLIKELY(rc < 0))
        {
            RTMemTmpFree(pvBuf);
            LogRel((DRIVER_NAME ":VBoxGuestHaikuIOCtl: user_memcpy failed; pvBuf=%p data=%p op=%d. rc=%d\n", pvBuf, data, op, rc));
            return EFAULT;
        }
        if (RT_UNLIKELY(!VALID_PTR(pvBuf)))
        {
            RTMemTmpFree(pvBuf);
            LogRel((DRIVER_NAME ":VBoxGuestHaikuIOCtl: pvBuf invalid pointer %p\n", pvBuf));
            return EINVAL;
        }
    }
    Log((DRIVER_NAME ":VBoxGuestHaikuIOCtl: pSession=%p pid=%d.\n", pSession, (int)RTProcSelf()));

    /*
     * Process the IOCtl.
     */
    size_t cbDataReturned;
    rc = VBoxGuestCommonIOCtl(op, &g_DevExt, pSession, pvBuf, len, &cbDataReturned);
    if (RT_SUCCESS(rc))
    {
        rc = 0;
        if (RT_UNLIKELY(cbDataReturned > len))
        {
            Log((DRIVER_NAME ":VBoxGuestHaikuIOCtl: too much output data %d expected %d\n", cbDataReturned, len));
            cbDataReturned = len;
        }
        if (cbDataReturned > 0)
        {
            rc = user_memcpy(data, pvBuf, cbDataReturned);
            if (RT_UNLIKELY(rc < 0))
            {
                Log((DRIVER_NAME ":VBoxGuestHaikuIOCtl: user_memcpy failed; pvBuf=%p pArg=%p Cmd=%lu. rc=%d\n", pvBuf, data, op, rc));
                rc = EFAULT;
            }
        }
    }
    else
    {
        Log((DRIVER_NAME ":VBoxGuestHaikuIOCtl: VBoxGuestCommonIOCtl failed. rc=%d\n", rc));
        rc = EFAULT;
    }
    RTMemTmpFree(pvBuf);
    return rc;
#if 0
#endif
}

static status_t VBoxGuestHaikuSelect (void *cookie, uint8 event, uint32 ref, selectsync *sync)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
	RTSPINLOCKTMP tmp;
	status_t err = B_OK;
   	//dprintf(DRIVER_NAME "select(,%d,%p)\n", event, sync);


	switch (event) {
		case B_SELECT_READ:
		//case B_SELECT_ERROR:
			break;
		default:
			return EINVAL;
	}

	RTSpinlockAcquireNoInts(g_DevExt.SessionSpinlock, &tmp);
	
    uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    if (pSession->u32MousePosChangedSeq != u32CurSeq)
    {
    	//dprintf(DRIVER_NAME "select: notifying now: %p %x\n", sync, event);
        pSession->u32MousePosChangedSeq = u32CurSeq;
		notify_select_event(sync, event);
    } else if (sState.selectSync == NULL) {
    	//dprintf(DRIVER_NAME "select: caching: %p %x\n", sync, event);
		sState.selectEvent = (uint8_t)event;
		sState.selectRef = (uint32_t)ref;
		sState.selectSync = (void *)sync;
	} else {
    	//dprintf(DRIVER_NAME "select: dropping: %p %x\n", sync, event);
		err = B_WOULD_BLOCK;
	}
	
	RTSpinlockReleaseNoInts(g_DevExt.SessionSpinlock, &tmp);

    return err;
#if 0
    int fEventsProcessed;

    LogFlow((DRIVER_NAME "::Poll: fEvents=%d\n", fEvents));

    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pDev->si_drv1;
    if (RT_UNLIKELY(!VALID_PTR(pSession))) {
        Log((DRIVER_NAME "::Poll: no state data for %s\n", devtoname(pDev)));
        return (fEvents & (POLLHUP|POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM));
    }

    uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    if (pSession->u32MousePosChangedSeq != u32CurSeq)
    {
        fEventsProcessed = fEvents & (POLLIN | POLLRDNORM);
        pSession->u32MousePosChangedSeq = u32CurSeq;
    }
    else
    {
        fEventsProcessed = 0;

        selrecord(td, &g_SelInfo);
    }

    return fEventsProcessed;
#endif
}

static status_t VBoxGuestHaikuDeselect (void *cookie, uint8 event, selectsync *sync)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;
	RTSPINLOCKTMP tmp;
	status_t err = B_OK;
   	//dprintf(DRIVER_NAME "deselect(,%d,%p)\n", event, sync);

	RTSpinlockAcquireNoInts(g_DevExt.SessionSpinlock, &tmp);
	
	if (sState.selectSync == sync) {
    	//dprintf(DRIVER_NAME "deselect: dropping: %p %x\n", sState.selectSync, sState.selectEvent);
		sState.selectEvent = (uint8_t)0;
		sState.selectRef = (uint32_t)0;
		sState.selectSync = NULL;
	} else
		err = B_OK;
	
	RTSpinlockReleaseNoInts(g_DevExt.SessionSpinlock, &tmp);
    return err;
}

static status_t VBoxGuestHaikuWrite (void *cookie, off_t position, const void *data, size_t *numBytes)
{
    *numBytes = 0;
    return 0;
}

static status_t VBoxGuestHaikuRead (void *cookie, off_t position, void *data, size_t *numBytes)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)cookie;

   	//dprintf(DRIVER_NAME "read(,,%d)\n", *numBytes);

    if (*numBytes == 0)
    	return 0;

    uint32_t u32CurSeq = ASMAtomicUoReadU32(&g_DevExt.u32MousePosChangedSeq);
    if (pSession->u32MousePosChangedSeq != u32CurSeq) {
        pSession->u32MousePosChangedSeq = u32CurSeq;
        //dprintf(DRIVER_NAME "read: giving 1 byte\n");
        *numBytes = 1;
        return 0;
    }

    *numBytes = 0;
	return 0;
}


static status_t VBoxGuestHaikuDetach(void)
{
    struct VBoxGuestDeviceState *pState = &sState;

    if (cUsers > 0)
        return EBUSY;

    /*
     * Reverse what we did in VBoxGuestHaikuAttach.
     */

    VBoxGuestHaikuRemoveIRQ(pState);

    if (pState->iVMMDevMemAreaId)
    	delete_area(pState->iVMMDevMemAreaId);

    VBoxGuestDeleteDevExt(&g_DevExt);

#ifdef DO_LOG
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
    RTLogSetDefaultInstance(NULL);
//    RTLogDestroy(RTLogSetDefaultInstance(NULL));
#endif

    RTR0Term();

    return B_OK;
}

/**
 * Interrupt service routine.
 *
 * @returns Whether the interrupt was from VMMDev.
 * @param   pvState Opaque pointer to the device state.
 */
static int32 VBoxGuestHaikuISR(void *pvState)
{
    LogFlow((DRIVER_NAME ":VBoxGuestHaikuISR pvState=%p\n", pvState));

    bool fOurIRQ = VBoxGuestCommonISR(&g_DevExt);

    return fOurIRQ ? B_HANDLED_INTERRUPT : B_UNHANDLED_INTERRUPT;
}

void VBoxGuestNativeISRMousePollEvent(PVBOXGUESTDEVEXT pDevExt)
{
    LogFlow((DRIVER_NAME "::NativeISRMousePollEvent:\n"));

	RTSPINLOCKTMP tmp;
	status_t err = B_OK;
	//dprintf(DRIVER_NAME ": isr mouse\n");

    /*
     * Wake up poll waiters.
     */
    //selwakeup(&g_SelInfo);
    //XXX:notify_select_event();
	RTSpinlockAcquire(g_DevExt.SessionSpinlock, &tmp);
	
	if (sState.selectSync) {
		//dprintf(DRIVER_NAME ": isr mouse: notify\n");
		notify_select_event(sState.selectSync, sState.selectEvent);
		sState.selectEvent = (uint8_t)0;
		sState.selectRef = (uint32_t)0;
		sState.selectSync = NULL;
	} else
		err = B_ERROR;
	
	RTSpinlockRelease(g_DevExt.SessionSpinlock, &tmp);
}

/**
 * Sets IRQ for VMMDev.
 *
 * @returns Haiku error code.
 * @param   pvState  Pointer to the state info structure.
 */
static int VBoxGuestHaikuAddIRQ(void *pvState)
{
    status_t status;
    struct VBoxGuestDeviceState *pState = (struct VBoxGuestDeviceState *)pvState;

	status = install_io_interrupt_handler(pState->iIrqResId, VBoxGuestHaikuISR, pState,  0);

    if (status != B_OK)
    {
        return VERR_DEV_IO_ERROR;
    }

    return VINF_SUCCESS;
}

/**
 * Removes IRQ for VMMDev.
 *
 * @param   pvState  Opaque pointer to the state info structure.
 */
static void VBoxGuestHaikuRemoveIRQ(void *pvState)
{
    struct VBoxGuestDeviceState *pState = (struct VBoxGuestDeviceState *)pvState;

    remove_io_interrupt_handler(pState->iIrqResId, VBoxGuestHaikuISR, pState);
}

static status_t VBoxGuestHaikuAttach(const pci_info *pDevice)
{
    status_t status;
    int rc = VINF_SUCCESS;
    int iResId = 0;
    struct VBoxGuestDeviceState *pState = &sState;
    static const char * const   s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    PRTLOGGER                   pRelLogger;

    cUsers = 0;

    /*
     * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
     */
    rc = RTR0Init(0);
    if (RT_FAILURE(rc))
    {
        LogFunc(("RTR0Init failed.\n"));
        return ENXIO;
    }

#ifdef DO_LOG
    /*
     * Create the release log.
     * (We do that here instead of common code because we want to log
     * early failures using the LogRel macro.)
     */
    rc = RTLogCreate(&pRelLogger, 0|RTLOGFLAGS_PREFIX_THREAD /* fFlags */, "all",
                     "VBOX_RELEASE_LOG", RT_ELEMENTS(s_apszGroups), s_apszGroups,
                     RTLOGDEST_STDOUT | RTLOGDEST_DEBUGGER | RTLOGDEST_USER, NULL);
dprintf(DRIVER_NAME ": RTLogCreate: %d\n", rc);
    if (RT_SUCCESS(rc))
    {
        //RTLogGroupSettings(pRelLogger, g_szLogGrp);
        //RTLogFlags(pRelLogger, g_szLogFlags);
        //RTLogDestinations(pRelLogger, "/var/log/vboxguest.log");
        RTLogRelSetDefaultInstance(pRelLogger);
        RTLogSetDefaultInstance(pRelLogger);//XXX
    }
#endif
	Log((DRIVER_NAME ": plip!\n"));
	LogAlways((DRIVER_NAME ": plop!\n"));

    /*
     * Allocate I/O port resource.
     */
    pState->uIOPortBase = pDevice->u.h0.base_registers[0];
    //XXX check flags for IO ?
    if (pState->uIOPortBase)
    {
        /*
         * Map the MMIO region.
         */
	    uint32 phys = pDevice->u.h0.base_registers[1];
        //XXX check flags for mem ?
        pState->VMMDevMemSize    = pDevice->u.h0.base_register_sizes[1];
        pState->iVMMDevMemAreaId = map_physical_memory("VirtualBox Guest MMIO",
        	phys, pState->VMMDevMemSize, B_ANY_KERNEL_BLOCK_ADDRESS,
        	B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &pState->pMMIOBase);

        if (pState->iVMMDevMemAreaId > 0 && pState->pMMIOBase)
        {
            /*
             * Call the common device extension initializer.
             */
            rc = VBoxGuestInitDevExt(&g_DevExt, pState->uIOPortBase,
                                     pState->pMMIOBase, pState->VMMDevMemSize,
#if ARCH_BITS == 64
                                     VBOXOSTYPE_Haiku_x64,
#else
                                     VBOXOSTYPE_Haiku,
#endif
                                     VMMDEV_EVENT_MOUSE_POSITION_CHANGED);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Add IRQ of VMMDev.
                 */
                pState->iIrqResId = pDevice->u.h0.interrupt_line;
                rc = VBoxGuestHaikuAddIRQ(pState);
                if (RT_SUCCESS(rc))
                {
                    dprintf(DRIVER_NAME ": loaded successfully\n");
                    return 0;
                }
                else
                    dprintf((DRIVER_NAME ":VBoxGuestInitDevExt failed.\n"));
                VBoxGuestDeleteDevExt(&g_DevExt);
            }
            else
                dprintf((DRIVER_NAME ":VBoxGuestHaikuAddIRQ failed.\n"));
        }
        else
            dprintf((DRIVER_NAME ":MMIO region setup failed.\n"));
    }
    else
        dprintf((DRIVER_NAME ":IOport setup failed.\n"));

    RTR0Term();
    return ENXIO;
}

static status_t VBoxGuestHaikuProbe(pci_info *pDevice)
{
    if ((pDevice->vendor_id == VMMDEV_VENDORID) && (pDevice->device_id == VMMDEV_DEVICEID))
        return 0;

    return ENXIO;
}

/* BeOS-style driver entry points */

status_t init_hardware(void)
{
	status_t status = B_ENTRY_NOT_FOUND;
	pci_info info;
	int ix = 0;

	if (get_module (B_PCI_MODULE_NAME, (module_info **)&gPCI))
		return ENOSYS;

	while ((*gPCI->get_nth_pci_info)(ix++, &info) == B_OK) {
		if (VBoxGuestHaikuProbe(&info) == 0) {
			// we found it
			dprintf(DRIVER_NAME ": found VirtualBox Guest PCI Device\n");
			status = B_OK;
			break;
		}
	}

	put_module(B_PCI_MODULE_NAME);

    return B_OK;
}

const char **publish_devices(void)
{
    static const char *names[] = { DEVICE_NAME , NULL };
    return names;
}

device_hooks *find_device(const char* name)
{
    return &g_VBoxGuestHaikuDeviceHooks;
}

status_t init_driver(void)
{
	status_t status = B_ENTRY_NOT_FOUND;
	pci_info info;
	int ix = 0;

	if (get_module (B_PCI_MODULE_NAME, (module_info **)&gPCI))
		return ENOSYS;

	while ((*gPCI->get_nth_pci_info)(ix++, &info) == B_OK) {
		if (VBoxGuestHaikuProbe(&info) == 0) {
			// we found it
			status = VBoxGuestHaikuAttach(&info);
			break;
		}
	}

    return status;
}

void uninit_driver(void)
{
	VBoxGuestHaikuDetach();

	put_module(B_PCI_MODULE_NAME);
}



/* Common code that depend on g_DevExt. */
#include "VBoxGuestIDC-unix.c.h"

