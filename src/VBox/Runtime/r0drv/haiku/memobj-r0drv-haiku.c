/* $Id: memobj-r0drv-haiku.c 33540 2010-10-28 09:27:05Z vboxsync $ */
/** @file
 * IPRT - Ring-0 Memory Objects, Haiku.
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

#include <iprt/memobj.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include "internal/memobj.h"

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * The Haiku version of the memory object structure.
 */
typedef struct RTR0MEMOBJHAIKU
{
    /** The core structure. */
    RTR0MEMOBJINTERNAL  Core;
    /** Area identifier */
    area_id             fArea;
} RTR0MEMOBJHAIKU, *PRTR0MEMOBJHAIKU;


//MALLOC_DEFINE(M_IPRTMOBJ, "iprtmobj", "IPRT - R0MemObj");
/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/

/**
 * Gets the virtual memory map the specified object is mapped into.
 *
 * @returns VM map handle on success, NULL if no map.
 * @param   pMem                The memory object.
 */
#if 0
static vm_map_t rtR0MemObjHaikuGetMap(PRTR0MEMOBJINTERNAL pMem)
{
    switch (pMem->enmType)
    {
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
            return kernel_map;

        case RTR0MEMOBJTYPE_PHYS:
        case RTR0MEMOBJTYPE_PHYS_NC:
            return NULL; /* pretend these have no mapping atm. */

        case RTR0MEMOBJTYPE_LOCK:
            return pMem->u.Lock.R0Process == NIL_RTR0PROCESS
                ? kernel_map
                : &((struct proc *)pMem->u.Lock.R0Process)->p_vmspace->vm_map;

        case RTR0MEMOBJTYPE_RES_VIRT:
            return pMem->u.ResVirt.R0Process == NIL_RTR0PROCESS
                ? kernel_map
                : &((struct proc *)pMem->u.ResVirt.R0Process)->p_vmspace->vm_map;

        case RTR0MEMOBJTYPE_MAPPING:
            return pMem->u.Mapping.R0Process == NIL_RTR0PROCESS
                ? kernel_map
                : &((struct proc *)pMem->u.Mapping.R0Process)->p_vmspace->vm_map;

        default:
            return NULL;
    }
}
#endif

int rtR0MemObjNativeFree(RTR0MEMOBJ pMem)
{
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)pMem;
    int rc = B_OK;
    dprintf("%s(%p {type: %d})\n", __FUNCTION__, pMem, pMemHaiku->Core.enmType);

    switch (pMemHaiku->Core.enmType)
    {
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
        case RTR0MEMOBJTYPE_MAPPING:
        case RTR0MEMOBJTYPE_PHYS:
        case RTR0MEMOBJTYPE_PHYS_NC:
        {
        	if (pMemHaiku->fArea > -1)
				rc = delete_area(pMemHaiku->fArea);

            AssertMsg(rc == B_OK, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_LOCK:
        {
            team_id team = B_SYSTEM_TEAM;

            if (pMemHaiku->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                team = ((team_id)pMemHaiku->Core.u.Lock.R0Process);

            rc = unlock_memory_etc(team, pMemHaiku->Core.pv,
                               pMemHaiku->Core.cb,
                               B_READ_DEVICE);

            AssertMsg(rc == B_OK, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_RES_VIRT:
        {
            team_id team = B_SYSTEM_TEAM;

            if (pMemHaiku->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                team = ((team_id)pMemHaiku->Core.u.Lock.R0Process);

        	rc = vm_unreserve_address_range(team, pMemHaiku->Core.pv,
        						pMemHaiku->Core.cb);

            AssertMsg(rc == B_OK, ("%#x", rc));
            break;
        }

        default:
            AssertMsgFailed(("enmType=%d\n", pMemHaiku->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}

static int rtR0MemObjNativeAllocArea(PPRTR0MEMOBJINTERNAL ppMem, size_t cb,
	bool fExecutable, RTR0MEMOBJTYPE type, RTHCPHYS PhysHighest, size_t uAlignment)
{
    int rc;
    void *MapAddress;
    const char *name;
    uint32 addressSpec = B_ANY_KERNEL_ADDRESS;
    uint32 lock;
    dprintf("%s(%p, %d, %c, type: %d, %08x, %x)\n", __FUNCTION__, ppMem, cb, fExecutable ? 't' : 'f', type, PhysHighest, uAlignment);

    switch (type)
    {
        case RTR0MEMOBJTYPE_PAGE:
        	name = "IPRT R0MemObj Alloc";
        	lock = B_FULL_LOCK;
        	break;
        case RTR0MEMOBJTYPE_LOW:
        	name = "IPRT R0MemObj AllocLow";
        	lock = B_32_BIT_FULL_LOCK;
        	break;
        case RTR0MEMOBJTYPE_CONT:
        	name = "IPRT R0MemObj AllocCont";
        	lock = B_32_BIT_CONTIGUOUS;
        	break;
        //case RTR0MEMOBJTYPE_MAPPING:
        //	lock = B_FULL_LOCK;
        //	break;
        case RTR0MEMOBJTYPE_PHYS:
		    /** @todo alignment  */
    		if (uAlignment != PAGE_SIZE)
        		return VERR_NOT_SUPPORTED;
        case RTR0MEMOBJTYPE_PHYS_NC:
        	name = "IPRT R0MemObj AllocPhys";
        	lock = (PhysHighest < _4G ? B_LOMEM : B_32_BIT_CONTIGUOUS);
        	break;
        //case RTR0MEMOBJTYPE_LOCK:
        //	break;
        default:
        	return VERR_INTERNAL_ERROR;
    }

    /* create the object. */
    PRTR0MEMOBJHAIKU pMemHaiku;
    pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(RTR0MEMOBJHAIKU),
    	type, NULL, cb);

    if (!pMemHaiku)
        return VERR_NO_MEMORY;

    NOREF(fExecutable); // ignored for now
    rc = pMemHaiku->fArea = create_area(name,
    	&MapAddress, addressSpec, cb, lock, B_READ_AREA | B_WRITE_AREA);

    if (rc == B_OK)
    {
    	physical_entry physMap[2];
        /* Store start address */
        pMemHaiku->Core.pv = MapAddress;

	    switch (type)
	    {
	        case RTR0MEMOBJTYPE_CONT:
	        	rc = get_memory_map(MapAddress, cb, physMap, 2);
	        	if (rc == B_OK)
		        	pMemHaiku->Core.u.Cont.Phys = physMap[0].address;
		        break;
	        case RTR0MEMOBJTYPE_PHYS:
	        case RTR0MEMOBJTYPE_PHYS_NC:
	        	rc = get_memory_map(MapAddress, cb, physMap, 2);
	        	if (rc == B_OK) {
        			pMemHaiku->Core.u.Phys.PhysBase = physMap[0].address;
        			pMemHaiku->Core.u.Phys.fAllocated = true;
	        	}
		    	break;
		    default:
		    	break;
	    }

        if (rc >= B_OK) {
        	*ppMem = &pMemHaiku->Core;
        	return VINF_SUCCESS;
        }

       	delete_area(pMemHaiku->fArea);
    }

    rtR0MemObjDelete(&pMemHaiku->Core);
    return RTErrConvertFromHaikuKernReturn(rc);
}

int rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
	return rtR0MemObjNativeAllocArea(ppMem, cb, fExecutable, 
		RTR0MEMOBJTYPE_PAGE, 0, 0);
}

int rtR0MemObjNativeAllocLow(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
	return rtR0MemObjNativeAllocArea(ppMem, cb, fExecutable, 
		RTR0MEMOBJTYPE_LOW, 0, 0);
}


int rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable)
{
	return rtR0MemObjNativeAllocArea(ppMem, cb, fExecutable, 
		RTR0MEMOBJTYPE_CONT, 0, 0);
}

int rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment)
{
	return rtR0MemObjNativeAllocArea(ppMem, cb, false, 
		RTR0MEMOBJTYPE_PHYS, PhysHighest, uAlignment);
}


int rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest)
{
	//return rtR0MemObjNativeAllocArea(ppMem, cb, false, 
	//	RTR0MEMOBJTYPE_PHYS_NC, PhysHighest, PAGE_SIZE);
    return rtR0MemObjNativeAllocPhys(ppMem, cb, PhysHighest, PAGE_SIZE);
}


int rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy)
{
    AssertReturn(uCachePolicy == RTMEM_CACHE_POLICY_DONT_CARE, VERR_NOT_SUPPORTED);
    dprintf("%s(%p, %08x, %d, %x)\n", __FUNCTION__, ppMem, Phys, cb, uCachePolicy);

    /* create the object. */
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(*pMemHaiku), RTR0MEMOBJTYPE_PHYS, NULL, cb);
    if (!pMemHaiku)
        return VERR_NO_MEMORY;

    /* there is no allocation here, it needs to be mapped somewhere first. */
    pMemHaiku->fArea = -1;
    pMemHaiku->Core.u.Phys.fAllocated = false;
    pMemHaiku->Core.u.Phys.PhysBase = Phys;
    pMemHaiku->Core.u.Phys.uCachePolicy = uCachePolicy;
    *ppMem = &pMemHaiku->Core;
    return VINF_SUCCESS;
}


/**
 * Worker locking the memory in either kernel or user maps.
 */
static int rtR0MemObjNativeLockInMap(PPRTR0MEMOBJINTERNAL ppMem,
                                     void *AddrStart, size_t cb,
                                     uint32_t fAccess,
                                     RTR0PROCESS R0Process, int fFlags)
{
    int rc;
    team_id team = B_SYSTEM_TEAM;
    NOREF(fAccess);
    dprintf("%s(%p, %p, %d, %x, %d, %x)\n", __FUNCTION__, ppMem, AddrStart, cb, fAccess, R0Process, fFlags);

    /* create the object. */
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(*pMemHaiku), RTR0MEMOBJTYPE_LOCK, (void *)AddrStart, cb);
    if (!pMemHaiku)
        return VERR_NO_MEMORY;

	if (R0Process != NIL_RTR0PROCESS)
		team = (team_id)R0Process;

	pMemHaiku->fArea = -1;

    rc = lock_memory_etc(team, AddrStart, cb, fFlags);
    if (rc == B_OK)
    {
        pMemHaiku->Core.u.Lock.R0Process = R0Process;
        *ppMem = &pMemHaiku->Core;
        return VINF_SUCCESS;
    }
    rtR0MemObjDelete(&pMemHaiku->Core);
    return RTErrConvertFromHaikuKernReturn(rc);
}


int rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess, RTR0PROCESS R0Process)
{
    return rtR0MemObjNativeLockInMap(ppMem, (void *)R3Ptr, cb, fAccess, R0Process,
                                     B_READ_DEVICE);
}


int rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, uint32_t fAccess)
{
    return rtR0MemObjNativeLockInMap(ppMem, pv, cb, fAccess, NIL_RTR0PROCESS,
                                     B_READ_DEVICE);
}


/**
 * Worker for the two virtual address space reservers.
 *
 * We're leaning on the examples provided by mmap and vm_mmap in vm_mmap.c here.
 */
static int rtR0MemObjNativeReserveInMap(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment, RTR0PROCESS R0Process)
{
    int rc;
    team_id team = B_SYSTEM_TEAM;

    dprintf("%s(%p, %p, %d, %x, %d)\n", __FUNCTION__, ppMem, pvFixed, cb, uAlignment, R0Process);

	if (R0Process != NIL_RTR0PROCESS)
		team = (team_id)R0Process;

    /*
     * Check that the specified alignment is supported.
     */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Create the object.
     */
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(*pMemHaiku), RTR0MEMOBJTYPE_RES_VIRT, NULL, cb);
    if (!pMemHaiku)
        return VERR_NO_MEMORY;

    /*
     * Ask the kernel to reserve the address range.
     */
    //XXX: vm_reserve_address_range ?
    return VERR_NOT_SUPPORTED;
}

int rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment)
{
    dprintf("%s() NOT_SUPPORTED\n", __FUNCTION__);
    return VERR_NOT_SUPPORTED;
//    return rtR0MemObjNativeReserveInMap(ppMem, pvFixed, cb, uAlignment, NIL_RTR0PROCESS);
}


int rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment, RTR0PROCESS R0Process)
{
    dprintf("%s() NOT_SUPPORTED\n", __FUNCTION__);
    return VERR_NOT_SUPPORTED;
//    return rtR0MemObjNativeReserveInMap(ppMem, (void *)R3PtrFixed, cb, uAlignment, R0Process);
}


int rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment,
                              unsigned fProt, size_t offSub, size_t cbSub)
{
    PRTR0MEMOBJHAIKU pMemToMapHaiku = (PRTR0MEMOBJHAIKU)pMemToMap;
    PRTR0MEMOBJHAIKU pMemHaiku;
    area_id area = -1;
	void *MapAddress = pvFixed;
	uint32 addrSpec = B_EXACT_ADDRESS;
	uint32 protection = 0;
    int rc = VERR_MAP_FAILED;
    AssertMsgReturn(!offSub && !cbSub, ("%#x %#x\n", offSub, cbSub), VERR_NOT_SUPPORTED);
    AssertMsgReturn(pvFixed == (void *)-1, ("%p\n", pvFixed), VERR_NOT_SUPPORTED);
    dprintf("%s(%p, %p, %p, %d, %x, %u, %u)\n", __FUNCTION__, ppMem, pMemToMap, pvFixed, uAlignment,
		fProt, offSub, cbSub);

    /*
     * Check that the specified alignment is supported.
     */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /* we can't map anything to the first page, sorry. */
    if (pvFixed == 0)
        return VERR_NOT_SUPPORTED;

	if (fProt & RTMEM_PROT_READ)
		protection |= B_KERNEL_READ_AREA;
	if (fProt & RTMEM_PROT_WRITE)
		protection |= B_KERNEL_WRITE_AREA;

	/*
	 * Either the object we map has an area associated with, which we can clone,
	 * or it's a physical address range which we must map.
	 */
	if (pMemToMapHaiku->fArea > -1) {
		
	    if (pvFixed == (void *)-1)
    		addrSpec = B_ANY_KERNEL_ADDRESS;

		rc = area = clone_area("IPRT R0MemObj MapKernel", &MapAddress,
			addrSpec, protection, pMemToMapHaiku->fArea);
		
		dprintf("%s: clone_area(,, %d, 0x%x, %d): %d 0x%08lx\n", __FUNCTION__,
			addrSpec, protection, pMemToMapHaiku->fArea, rc, rc);

	} else if (pMemToMapHaiku->Core.enmType == RTR0MEMOBJTYPE_PHYS) {

		/* map_physical_memory() won't let you choose where. */
	    if (pvFixed != (void *)-1)
	    	return VERR_NOT_SUPPORTED;
		addrSpec = B_ANY_KERNEL_ADDRESS;

		rc = area = map_physical_memory("IPRT R0MemObj MapKernelPhys",
			(phys_addr_t)pMemToMapHaiku->Core.u.Phys.PhysBase, pMemToMapHaiku->Core.cb,
			addrSpec, protection, &MapAddress);

		dprintf("%s: map_physical_memory(, %p, %d, %d, 0x%x,): %d 0x%08lx\n", __FUNCTION__,
			(void *)pMemToMapHaiku->Core.u.Phys.PhysBase, pMemToMapHaiku->Core.cb,
			addrSpec, protection, rc, rc);

	} else
		return VERR_NOT_SUPPORTED;

	if (rc >= B_OK) {

	    /* create the object. */
   		pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(RTR0MEMOBJHAIKU),
   			RTR0MEMOBJTYPE_MAPPING, MapAddress, pMemToMapHaiku->Core.cb);

	    if (!pMemHaiku)
       		return VERR_NO_MEMORY;

        pMemHaiku->Core.u.Mapping.R0Process = NIL_RTR0PROCESS;
        pMemHaiku->Core.pv = MapAddress;
        pMemHaiku->fArea = area;
       	*ppMem = &pMemHaiku->Core;
       	dprintf("%s: OK\n", __FUNCTION__);
		return VINF_SUCCESS;
	} else
		dprintf("%s: 0x%08lx\n", __FUNCTION__, rc);

	//rc = RTErrConvertFromHaikuKernReturn(rc);
	rc = VERR_MAP_FAILED;

/** @todo finish the implementation. */

    rtR0MemObjDelete(&pMemHaiku->Core);
   	dprintf("%s: %d\n", __FUNCTION__, rc);
    return rc;
//    return VERR_NOT_SUPPORTED;
}


/* see http://markmail.org/message/udhq33tefgtyfozs */
int rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, RTR3PTR R3PtrFixed, size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process)
{
    dprintf("%s() NOT_SUPPORTED\n", __FUNCTION__);
    return VERR_NOT_SUPPORTED;
#if 0
    /*
     * Check for unsupported stuff.
     */
    AssertMsgReturn(R0Process == RTR0ProcHandleSelf(), ("%p != %p\n", R0Process, RTR0ProcHandleSelf()), VERR_NOT_SUPPORTED);
    AssertMsgReturn(R3PtrFixed == (RTR3PTR)-1, ("%p\n", R3PtrFixed), VERR_NOT_SUPPORTED);
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    int                rc;
    PRTR0MEMOBJHAIKU pMemToMapHaiku = (PRTR0MEMOBJHAIKU)pMemToMap;
    struct proc       *pProc            = (struct proc *)R0Process;
    struct vm_map     *pProcMap         = &pProc->p_vmspace->vm_map;

    /* calc protection */
    vm_prot_t       ProtectionFlags = 0;
    if ((fProt & RTMEM_PROT_NONE) == RTMEM_PROT_NONE)
        ProtectionFlags = VM_PROT_NONE;
    if ((fProt & RTMEM_PROT_READ) == RTMEM_PROT_READ)
        ProtectionFlags |= VM_PROT_READ;
    if ((fProt & RTMEM_PROT_WRITE) == RTMEM_PROT_WRITE)
        ProtectionFlags |= VM_PROT_WRITE;
    if ((fProt & RTMEM_PROT_EXEC) == RTMEM_PROT_EXEC)
        ProtectionFlags |= VM_PROT_EXECUTE;

    /* calc mapping address */
    PROC_LOCK(pProc);
    vm_offset_t AddrR3 = round_page((vm_offset_t)pProc->p_vmspace->vm_daddr + lim_max(pProc, RLIMIT_DATA));
    PROC_UNLOCK(pProc);

    /* Insert the object in the map. */
    rc = vm_map_find(pProcMap,              /* Map to insert the object in */
                     NULL,                  /* Object to map */
                     0,                     /* Start offset in the object */
                     &AddrR3,               /* Start address IN/OUT */
                     pMemToMap->cb,         /* Size of the mapping */
                     TRUE,                  /* Whether a suitable address should be searched for first */
                     ProtectionFlags,       /* protection flags */
                     VM_PROT_ALL,           /* Maximum protection flags */
                     0);                    /* Copy on write */

    /* Map the memory page by page into the destination map. */
    if (rc == KERN_SUCCESS)
    {
        size_t         cPages       = pMemToMap->cb >> PAGE_SHIFT;;
        pmap_t         pPhysicalMap = pProcMap->pmap;
        vm_offset_t    AddrR3Dst    = AddrR3;

        if (   pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS
            || pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS_NC
            || pMemToMap->enmType == RTR0MEMOBJTYPE_PAGE)
        {
            /* Mapping physical allocations */
            Assert(cPages == pMemToMapHaiku->u.Phys.cPages);

            /* Insert the memory page by page into the mapping. */
            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                vm_page_t pPage = pMemToMapHaiku->u.Phys.apPages[iPage];

                MY_PMAP_ENTER(pPhysicalMap, AddrR3Dst, pPage, ProtectionFlags, TRUE);
                AddrR3Dst += PAGE_SIZE;
            }
        }
        else
        {
            /* Mapping cont or low memory types */
            vm_offset_t AddrToMap = (vm_offset_t)pMemToMap->pv;

            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                vm_page_t pPage = PHYS_TO_VM_PAGE(vtophys(AddrToMap));

                MY_PMAP_ENTER(pPhysicalMap, AddrR3Dst, pPage, ProtectionFlags, TRUE);
                AddrR3Dst += PAGE_SIZE;
                AddrToMap += PAGE_SIZE;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        /*
         * Create a mapping object for it.
         */
        PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(RTR0MEMOBJHAIKU),
                                                                           RTR0MEMOBJTYPE_MAPPING,
                                                                           (void *)AddrR3,
                                                                           pMemToMap->cb);
        if (pMemHaiku)
        {
            Assert((vm_offset_t)pMemHaiku->Core.pv == AddrR3);
            pMemHaiku->Core.u.Mapping.R0Process = R0Process;
            *ppMem = &pMemHaiku->Core;
            return VINF_SUCCESS;
        }

        rc = vm_map_remove(pProcMap, ((vm_offset_t)AddrR3), ((vm_offset_t)AddrR3) + pMemToMap->cb);
        AssertMsg(rc == KERN_SUCCESS, ("Deleting mapping failed\n"));
    }
#endif
    return VERR_NO_MEMORY;
}


int rtR0MemObjNativeProtect(PRTR0MEMOBJINTERNAL pMem, size_t offSub, size_t cbSub, uint32_t fProt)
{
    dprintf("%s() NOT_SUPPORTED\n", __FUNCTION__);
    return VERR_NOT_SUPPORTED;
}


RTHCPHYS rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, size_t iPage)
{
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)pMem;
    status_t rc;

    dprintf("%s(%p {type: %d}, %d)\n", __FUNCTION__, pMem, pMemHaiku->Core.enmType, iPage);

    switch (pMemHaiku->Core.enmType)
    {
        case RTR0MEMOBJTYPE_LOCK:
        {
		  	team_id        team = B_SYSTEM_TEAM;
		  	physical_entry physMap[2];
		  	int32          physMapEntries = 2;

            if (pMemHaiku->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
            	team = (team_id)pMemHaiku->Core.u.Lock.R0Process;

            void *pb = pMemHaiku->Core.pv + (iPage << PAGE_SHIFT);

			rc = get_memory_map_etc(team, pb, B_PAGE_SIZE, physMap, &physMapEntries);
			if (rc < B_OK || physMapEntries < 1)
				return NIL_RTHCPHYS;

            return physMap[0].address;
        }

#if 0
        case RTR0MEMOBJTYPE_MAPPING:
        {
            vm_offset_t pb = (vm_offset_t)pMemHaiku->Core.pv + (iPage << PAGE_SHIFT);

            if (pMemHaiku->Core.u.Mapping.R0Process != NIL_RTR0PROCESS)
            {
                struct proc    *pProc     = (struct proc *)pMemHaiku->Core.u.Mapping.R0Process;
                struct vm_map  *pProcMap  = &pProc->p_vmspace->vm_map;
                pmap_t pPhysicalMap       = pProcMap->pmap;

                return pmap_extract(pPhysicalMap, pb);
            }
            return vtophys(pb);
        }
#endif
        case RTR0MEMOBJTYPE_CONT:
            return pMemHaiku->Core.u.Cont.Phys + (iPage << PAGE_SHIFT);

        case RTR0MEMOBJTYPE_PHYS:
            return pMemHaiku->Core.u.Phys.PhysBase + (iPage << PAGE_SHIFT);

        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_PHYS_NC:
        {
		  	team_id        team = B_SYSTEM_TEAM;
		  	physical_entry physMap[2];
		  	int32          physMapEntries = 2;

            void *pb = pMemHaiku->Core.pv + (iPage << PAGE_SHIFT);

			rc = get_memory_map_etc(team, pb, B_PAGE_SIZE, physMap, &physMapEntries);
			if (rc < B_OK || physMapEntries < 1)
				return NIL_RTHCPHYS;

            return physMap[0].address;
        }

        case RTR0MEMOBJTYPE_RES_VIRT:
        default:
            return NIL_RTHCPHYS;
    }
}

