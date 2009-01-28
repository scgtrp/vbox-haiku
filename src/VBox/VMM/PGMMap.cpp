/* $Id$ */
/** @file
 * PGM - Page Manager, Guest Context Mappings.
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
#define LOG_GROUP LOG_GROUP_PGM
#include <VBox/dbgf.h>
#include <VBox/pgm.h>
#include "PGMInternal.h"
#include <VBox/vm.h>

#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void pgmR3MapClearPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iOldPDE);
static void pgmR3MapSetPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iNewPDE);
static int  pgmR3MapIntermediateCheckOne(PVM pVM, uintptr_t uAddress, unsigned cPages, PX86PT pPTDefault, PX86PTPAE pPTPaeDefault);
static void pgmR3MapIntermediateDoOne(PVM pVM, uintptr_t uAddress, RTHCPHYS HCPhys, unsigned cPages, PX86PT pPTDefault, PX86PTPAE pPTPaeDefault);
#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
static void pgmR3MapClearShadowPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iOldPDE);
static void pgmR3MapSetShadowPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iNewPDE);
#endif


/**
 * Creates a page table based mapping in GC.
 *
 * @returns VBox status code.
 * @param   pVM             VM Handle.
 * @param   GCPtr           Virtual Address. (Page table aligned!)
 * @param   cb              Size of the range. Must be a 4MB aligned!
 * @param   pfnRelocate     Relocation callback function.
 * @param   pvUser          User argument to the callback.
 * @param   pszDesc         Pointer to description string. This must not be freed.
 */
VMMR3DECL(int) PGMR3MapPT(PVM pVM, RTGCPTR GCPtr, uint32_t cb, PFNPGMRELOCATE pfnRelocate, void *pvUser, const char *pszDesc)
{
    LogFlow(("PGMR3MapPT: GCPtr=%#x cb=%d pfnRelocate=%p pvUser=%p pszDesc=%s\n", GCPtr, cb, pfnRelocate, pvUser, pszDesc));
    AssertMsg(pVM->pgm.s.pInterPD && pVM->pgm.s.pShwNestedRootR3, ("Paging isn't initialized, init order problems!\n"));

    /*
     * Validate input.
     */
    if (cb < _2M || cb > 64 * _1M)
    {
        AssertMsgFailed(("Serious? cb=%d\n", cb));
        return VERR_INVALID_PARAMETER;
    }
    cb = RT_ALIGN_32(cb, _4M);
    RTGCPTR GCPtrLast = GCPtr + cb - 1;
    if (GCPtrLast < GCPtr)
    {
        AssertMsgFailed(("Range wraps! GCPtr=%x GCPtrLast=%x\n", GCPtr, GCPtrLast));
        return VERR_INVALID_PARAMETER;
    }
    if (pVM->pgm.s.fMappingsFixed)
    {
        AssertMsgFailed(("Mappings are fixed! It's not possible to add new mappings at this time!\n"));
        return VERR_PGM_MAPPINGS_FIXED;
    }
    if (!pfnRelocate)
    {
        AssertMsgFailed(("Callback is required\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Find list location.
     */
    PPGMMAPPING pPrev = NULL;
    PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3;
    while (pCur)
    {
        if (pCur->GCPtrLast >= GCPtr && pCur->GCPtr <= GCPtrLast)
        {
            AssertMsgFailed(("Address is already in use by %s. req %#x-%#x take %#x-%#x\n",
                             pCur->pszDesc, GCPtr, GCPtrLast, pCur->GCPtr, pCur->GCPtrLast));
            LogRel(("VERR_PGM_MAPPING_CONFLICT: Address is already in use by %s. req %#x-%#x take %#x-%#x\n",
                    pCur->pszDesc, GCPtr, GCPtrLast, pCur->GCPtr, pCur->GCPtrLast));
            return VERR_PGM_MAPPING_CONFLICT;
        }
        if (pCur->GCPtr > GCPtr)
            break;
        pPrev = pCur;
        pCur = pCur->pNextR3;
    }

    /*
     * Check for conflicts with intermediate mappings.
     */
    const unsigned iPageDir = GCPtr >> X86_PD_SHIFT;
    const unsigned cPTs     = cb >> X86_PD_SHIFT;
    if (pVM->pgm.s.fFinalizedMappings)
    {
        for (unsigned i = 0; i < cPTs; i++)
            if (pVM->pgm.s.pInterPD->a[iPageDir + i].n.u1Present)
            {
                AssertMsgFailed(("Address %#x is already in use by an intermediate mapping.\n", GCPtr + (i << PAGE_SHIFT)));
                LogRel(("VERR_PGM_MAPPING_CONFLICT: Address %#x is already in use by an intermediate mapping.\n", GCPtr + (i << PAGE_SHIFT)));
                return VERR_PGM_MAPPING_CONFLICT;
            }
        /** @todo AMD64: add check in PAE structures too, so we can remove all the 32-Bit paging stuff there. */
    }

    /*
     * Allocate and initialize the new list node.
     */
    PPGMMAPPING pNew;
    int rc = MMHyperAlloc(pVM, RT_OFFSETOF(PGMMAPPING, aPTs[cPTs]), 0, MM_TAG_PGM, (void **)&pNew);
    if (RT_FAILURE(rc))
        return rc;
    pNew->GCPtr         = GCPtr;
    pNew->GCPtrLast     = GCPtrLast;
    pNew->cb            = cb;
    pNew->pszDesc       = pszDesc;
    pNew->pfnRelocate   = pfnRelocate;
    pNew->pvUser        = pvUser;
    pNew->cPTs          = cPTs;

    /*
     * Allocate page tables and insert them into the page directories.
     * (One 32-bit PT and two PAE PTs.)
     */
    uint8_t *pbPTs;
    rc = MMHyperAlloc(pVM, PAGE_SIZE * 3 * cPTs, PAGE_SIZE, MM_TAG_PGM, (void **)&pbPTs);
    if (RT_FAILURE(rc))
    {
        MMHyperFree(pVM, pNew);
        return VERR_NO_MEMORY;
    }

    /*
     * Init the page tables and insert them into the page directories.
     */
    Log4(("PGMR3MapPT: GCPtr=%RGv cPTs=%u pbPTs=%p\n", GCPtr, cPTs, pbPTs));
    for (unsigned i = 0; i < cPTs; i++)
    {
        /*
         * 32-bit.
         */
        pNew->aPTs[i].pPTR3    = (PX86PT)pbPTs;
        pNew->aPTs[i].pPTRC    = MMHyperR3ToRC(pVM, pNew->aPTs[i].pPTR3);
        pNew->aPTs[i].pPTR0    = MMHyperR3ToR0(pVM, pNew->aPTs[i].pPTR3);
        pNew->aPTs[i].HCPhysPT = MMR3HyperHCVirt2HCPhys(pVM, pNew->aPTs[i].pPTR3);
        pbPTs += PAGE_SIZE;
        Log4(("PGMR3MapPT: i=%d: pPTR3=%RHv pPTRC=%RRv pPRTR0=%RHv HCPhysPT=%RHp\n",
              i, pNew->aPTs[i].pPTR3, pNew->aPTs[i].pPTRC, pNew->aPTs[i].pPTR0, pNew->aPTs[i].HCPhysPT));

        /*
         * PAE.
         */
        pNew->aPTs[i].HCPhysPaePT0 = MMR3HyperHCVirt2HCPhys(pVM, pbPTs);
        pNew->aPTs[i].HCPhysPaePT1 = MMR3HyperHCVirt2HCPhys(pVM, pbPTs + PAGE_SIZE);
        pNew->aPTs[i].paPaePTsR3 = (PX86PTPAE)pbPTs;
        pNew->aPTs[i].paPaePTsRC = MMHyperR3ToRC(pVM, pbPTs);
        pNew->aPTs[i].paPaePTsR0 = MMHyperR3ToR0(pVM, pbPTs);
        pbPTs += PAGE_SIZE * 2;
        Log4(("PGMR3MapPT: i=%d: paPaePTsR#=%RHv paPaePTsRC=%RRv paPaePTsR#=%RHv HCPhysPaePT0=%RHp HCPhysPaePT1=%RHp\n",
              i, pNew->aPTs[i].paPaePTsR3, pNew->aPTs[i].paPaePTsRC, pNew->aPTs[i].paPaePTsR0, pNew->aPTs[i].HCPhysPaePT0, pNew->aPTs[i].HCPhysPaePT1));
    }
    if (pVM->pgm.s.fFinalizedMappings)
        pgmR3MapSetPDEs(pVM, pNew, iPageDir);
    /* else PGMR3FinalizeMappings() */

    /*
     * Insert the new mapping.
     */
    pNew->pNextR3 = pCur;
    pNew->pNextRC = pCur ? MMHyperR3ToRC(pVM, pCur) : NIL_RTRCPTR;
    pNew->pNextR0 = pCur ? MMHyperR3ToR0(pVM, pCur) : NIL_RTR0PTR;
    if (pPrev)
    {
        pPrev->pNextR3 = pNew;
        pPrev->pNextRC = MMHyperR3ToRC(pVM, pNew);
        pPrev->pNextR0 = MMHyperR3ToR0(pVM, pNew);
    }
    else
    {
        pVM->pgm.s.pMappingsR3 = pNew;
        pVM->pgm.s.pMappingsRC = MMHyperR3ToRC(pVM, pNew);
        pVM->pgm.s.pMappingsR0 = MMHyperR3ToR0(pVM, pNew);
    }

    VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
    return VINF_SUCCESS;
}


/**
 * Removes a page table based mapping.
 *
 * @returns VBox status code.
 * @param   pVM     VM Handle.
 * @param   GCPtr   Virtual Address. (Page table aligned!)
 */
VMMR3DECL(int)  PGMR3UnmapPT(PVM pVM, RTGCPTR GCPtr)
{
    LogFlow(("PGMR3UnmapPT: GCPtr=%#x\n", GCPtr));
    AssertReturn(pVM->pgm.s.fFinalizedMappings, VERR_WRONG_ORDER);

    /*
     * Find it.
     */
    PPGMMAPPING pPrev = NULL;
    PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3;
    while (pCur)
    {
        if (pCur->GCPtr == GCPtr)
        {
            /*
             * Unlink it.
             */
            if (pPrev)
            {
                pPrev->pNextR3 = pCur->pNextR3;
                pPrev->pNextRC = pCur->pNextRC;
                pPrev->pNextR0 = pCur->pNextR0;
            }
            else
            {
                pVM->pgm.s.pMappingsR3 = pCur->pNextR3;
                pVM->pgm.s.pMappingsRC = pCur->pNextRC;
                pVM->pgm.s.pMappingsR0 = pCur->pNextR0;
            }

            /*
             * Free the page table memory, clear page directory entries
             * and free the page tables and node memory.
             */
            MMHyperFree(pVM, pCur->aPTs[0].pPTR3);
            pgmR3MapClearPDEs(pVM, pCur, pCur->GCPtr >> X86_PD_SHIFT);
            MMHyperFree(pVM, pCur);

            VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
            return VINF_SUCCESS;
        }

        /* done? */
        if (pCur->GCPtr > GCPtr)
            break;

        /* next */
        pPrev = pCur;
        pCur = pCur->pNextR3;
    }

    AssertMsgFailed(("No mapping for %#x found!\n", GCPtr));
    return VERR_INVALID_PARAMETER;
}


/**
 * Checks whether a range of PDEs in the intermediate
 * memory context are unused.
 *
 * We're talking 32-bit PDEs here.
 *
 * @returns true/false.
 * @param   pVM         Pointer to the shared VM structure.
 * @param   iPD         The first PDE in the range.
 * @param   cPTs        The number of PDEs in the range.
 */
DECLINLINE(bool) pgmR3AreIntermediatePDEsUnused(PVM pVM, unsigned iPD, unsigned cPTs)
{
    if (pVM->pgm.s.pInterPD->a[iPD].n.u1Present)
        return false;
    while (cPTs > 1)
    {
        iPD++;
        if (pVM->pgm.s.pInterPD->a[iPD].n.u1Present)
            return false;
        cPTs--;
    }
    return true;
}


/**
 * Unlinks the mapping.
 *
 * The mapping *must* be in the list.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pMapping        The mapping to unlink.
 */
static void pgmR3MapUnlink(PVM pVM, PPGMMAPPING pMapping)
{
    PPGMMAPPING pAfterThis = pVM->pgm.s.pMappingsR3;
    if (pAfterThis == pMapping)
    {
        /* head */
        pVM->pgm.s.pMappingsR3 = pMapping->pNextR3;
        pVM->pgm.s.pMappingsRC = pMapping->pNextRC;
        pVM->pgm.s.pMappingsR0 = pMapping->pNextR0;
    }
    else
    {
        /* in the list */
        while (pAfterThis->pNextR3 != pMapping)
        {
            pAfterThis = pAfterThis->pNextR3;
            AssertReleaseReturnVoid(pAfterThis);
        }

        pAfterThis->pNextR3 = pMapping->pNextR3;
        pAfterThis->pNextRC = pMapping->pNextRC;
        pAfterThis->pNextR0 = pMapping->pNextR0;
    }
}


/**
 * Links the mapping.
 *
 * @param   pVM             Pointer to the shared VM structure.
 * @param   pMapping        The mapping to linked.
 */
static void pgmR3MapLink(PVM pVM, PPGMMAPPING pMapping)
{
    /*
     * Find the list location (it's sorted by GCPhys) and link it in.
     */
    if (    !pVM->pgm.s.pMappingsR3
        ||  pVM->pgm.s.pMappingsR3->GCPtr > pMapping->GCPtr)
    {
        /* head */
        pMapping->pNextR3 = pVM->pgm.s.pMappingsR3;
        pMapping->pNextRC = pVM->pgm.s.pMappingsRC;
        pMapping->pNextR0 = pVM->pgm.s.pMappingsR0;
        pVM->pgm.s.pMappingsR3 = pMapping;
        pVM->pgm.s.pMappingsRC = MMHyperR3ToRC(pVM, pMapping);
        pVM->pgm.s.pMappingsR0 = MMHyperR3ToR0(pVM, pMapping);
    }
    else
    {
        /* in the list */
        PPGMMAPPING pAfterThis  = pVM->pgm.s.pMappingsR3;
        PPGMMAPPING pBeforeThis = pAfterThis->pNextR3;
        while (pBeforeThis && pBeforeThis->GCPtr <= pMapping->GCPtr)
        {
            pAfterThis = pBeforeThis;
            pBeforeThis = pBeforeThis->pNextR3;
        }

        pMapping->pNextR3 = pAfterThis->pNextR3;
        pMapping->pNextRC = pAfterThis->pNextRC;
        pMapping->pNextR0 = pAfterThis->pNextR0;
        pAfterThis->pNextR3 = pMapping;
        pAfterThis->pNextRC = MMHyperR3ToRC(pVM, pMapping);
        pAfterThis->pNextR0 = MMHyperR3ToR0(pVM, pMapping);
    }
}


/**
 * Finalizes the intermediate context.
 *
 * This is called at the end of the ring-3 init and will construct the
 * intermediate paging structures, relocating all the mappings in the process.
 *
 * @returns VBox status code.
 * @param   pVM     Pointer to the shared VM structure.
 * @thread  EMT(0)
 */
VMMR3DECL(int) PGMR3FinalizeMappings(PVM pVM)
{
    AssertReturn(!pVM->pgm.s.fFinalizedMappings, VERR_WRONG_ORDER);
    pVM->pgm.s.fFinalizedMappings = true;

    /*
     * Loop until all mappings have been finalized.
     */
    /*unsigned    iPDNext = UINT32_C(0xc0000000) >> X86_PD_SHIFT;*/ /* makes CSAM/PATM freak out booting linux. :-/ */
#if 0
    unsigned    iPDNext = MM_HYPER_AREA_ADDRESS >> X86_PD_SHIFT;
#else
    unsigned    iPDNext = 1 << X86_PD_SHIFT; /* no hint, map them from the top. */
#endif
    PPGMMAPPING pCur;
    do
    {
        pCur = pVM->pgm.s.pMappingsR3;
        while (pCur)
        {
            if (!pCur->fFinalized)
            {
                /*
                 * Find a suitable location.
                 */
                RTGCPTR const   GCPtrOld = pCur->GCPtr;
                const unsigned  cPTs     = pCur->cPTs;
                unsigned        iPDNew   = iPDNext;
                if (    iPDNew + cPTs >= X86_PG_ENTRIES /* exclude the last PD */
                    ||  !pgmR3AreIntermediatePDEsUnused(pVM, iPDNew, cPTs)
                    ||  !pCur->pfnRelocate(pVM, GCPtrOld, (RTGCPTR)iPDNew << X86_PD_SHIFT, PGMRELOCATECALL_SUGGEST, pCur->pvUser))
                {
                    /* No luck, just scan down from 4GB-4MB, giving up at 4MB. */
                    iPDNew = X86_PG_ENTRIES - cPTs - 1;
                    while (     iPDNew > 0
                           &&   (   !pgmR3AreIntermediatePDEsUnused(pVM, iPDNew, cPTs)
                                 || !pCur->pfnRelocate(pVM, GCPtrOld, (RTGCPTR)iPDNew << X86_PD_SHIFT, PGMRELOCATECALL_SUGGEST, pCur->pvUser))
                           )
                        iPDNew--;
                    AssertLogRelReturn(iPDNew != 0, VERR_PGM_INTERMEDIATE_PAGING_CONFLICT);
                }

                /*
                 * Relocate it (something akin to pgmR3MapRelocate).
                 */
                pgmR3MapSetPDEs(pVM, pCur, iPDNew);

                /* unlink the mapping, update the entry and relink it. */
                pgmR3MapUnlink(pVM, pCur);

                RTGCPTR const GCPtrNew = (RTGCPTR)iPDNew << X86_PD_SHIFT;
                pCur->GCPtr      = GCPtrNew;
                pCur->GCPtrLast  = GCPtrNew + pCur->cb - 1;
                pCur->fFinalized = true;

                pgmR3MapLink(pVM, pCur);

                /* Finally work the callback. */
                pCur->pfnRelocate(pVM, GCPtrOld, GCPtrNew, PGMRELOCATECALL_RELOCATE, pCur->pvUser);

                /*
                 * The list order might have changed, start from the beginning again.
                 */
                iPDNext = iPDNew + cPTs;
                break;
            }

            /* next */
            pCur = pCur->pNextR3;
        }
    } while (pCur);

    return VINF_SUCCESS;
}


/**
 * Gets the size of the current guest mappings if they were to be
 * put next to oneanother.
 *
 * @returns VBox status code.
 * @param   pVM     The VM.
 * @param   pcb     Where to store the size.
 */
VMMR3DECL(int) PGMR3MappingsSize(PVM pVM, uint32_t *pcb)
{
    RTGCPTR cb = 0;
    for (PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3; pCur; pCur = pCur->pNextR3)
        cb += pCur->cb;

    *pcb = cb;
    AssertReturn(*pcb == cb, VERR_NUMBER_TOO_BIG);
    Log(("PGMR3MappingsSize: return %d (%#x) bytes\n", cb, cb));
    return VINF_SUCCESS;
}


/**
 * Fixes the guest context mappings in a range reserved from the Guest OS.
 *
 * @returns VBox status code.
 * @param   pVM         The VM.
 * @param   GCPtrBase   The address of the reserved range of guest memory.
 * @param   cb          The size of the range starting at GCPtrBase.
 */
VMMR3DECL(int) PGMR3MappingsFix(PVM pVM, RTGCPTR GCPtrBase, uint32_t cb)
{
    Log(("PGMR3MappingsFix: GCPtrBase=%#x cb=%#x\n", GCPtrBase, cb));

    /* Ignore the additions mapping fix call in VT-x/AMD-V. */
    if (    pVM->pgm.s.fMappingsFixed
        &&  HWACCMR3IsActive(pVM))
        return VINF_SUCCESS;

    /*
     * This is all or nothing at all. So, a tiny bit of paranoia first.
     */
    if (GCPtrBase & X86_PAGE_4M_OFFSET_MASK)
    {
        AssertMsgFailed(("GCPtrBase (%#x) has to be aligned on a 4MB address!\n", GCPtrBase));
        return VERR_INVALID_PARAMETER;
    }
    if (!cb || (cb & X86_PAGE_4M_OFFSET_MASK))
    {
        AssertMsgFailed(("cb (%#x) is 0 or not aligned on a 4MB address!\n", cb));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Before we do anything we'll do a forced PD sync to try make sure any
     * pending relocations because of these mappings have been resolved.
     */
    PGMSyncCR3(pVM, CPUMGetGuestCR0(pVM), CPUMGetGuestCR3(pVM), CPUMGetGuestCR4(pVM), true);

    /*
     * Check that it's not conflicting with a core code mapping in the intermediate page table.
     */
    unsigned    iPDNew = GCPtrBase >> X86_PD_SHIFT;
    unsigned    i = cb >> X86_PD_SHIFT;
    while (i-- > 0)
    {
        if (pVM->pgm.s.pInterPD->a[iPDNew + i].n.u1Present)
        {
            /* Check that it's not one or our mappings. */
            PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3;
            while (pCur)
            {
                if (iPDNew + i - (pCur->GCPtr >> X86_PD_SHIFT) < (pCur->cb >> X86_PD_SHIFT))
                    break;
                pCur = pCur->pNextR3;
            }
            if (!pCur)
            {
                LogRel(("PGMR3MappingsFix: Conflicts with intermediate PDE %#x (GCPtrBase=%RGv cb=%#zx). The guest should retry.\n",
                        iPDNew + i, GCPtrBase, cb));
                return VERR_PGM_MAPPINGS_FIX_CONFLICT;
            }
        }
    }

    /*
     * In PAE / PAE mode, make sure we don't cross page directories.
     */
    if (    (   pVM->pgm.s.enmGuestMode  == PGMMODE_PAE
             || pVM->pgm.s.enmGuestMode  == PGMMODE_PAE_NX)
        &&  (   pVM->pgm.s.enmShadowMode == PGMMODE_PAE
             || pVM->pgm.s.enmShadowMode == PGMMODE_PAE_NX))
    {
        unsigned iPdptBase = GCPtrBase >> X86_PDPT_SHIFT;
        unsigned iPdptLast = (GCPtrBase + cb - 1) >> X86_PDPT_SHIFT;
        if (iPdptBase != iPdptLast)
        {
            LogRel(("PGMR3MappingsFix: Crosses PD boundrary; iPdptBase=%#x iPdptLast=%#x (GCPtrBase=%RGv cb=%#zx). The guest should retry.\n",
                    iPdptBase, iPdptLast, GCPtrBase, cb));
            return VERR_PGM_MAPPINGS_FIX_CONFLICT;
        }
    }

    /*
     * Loop the mappings and check that they all agree on their new locations.
     */
    RTGCPTR     GCPtrCur = GCPtrBase;
    PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3;
    while (pCur)
    {
        if (!pCur->pfnRelocate(pVM, pCur->GCPtr, GCPtrCur, PGMRELOCATECALL_SUGGEST, pCur->pvUser))
        {
            AssertMsgFailed(("The suggested fixed address %#x was rejected by '%s'!\n", GCPtrCur, pCur->pszDesc));
            return VERR_PGM_MAPPINGS_FIX_REJECTED;
        }
        /* next */
        GCPtrCur += pCur->cb;
        pCur = pCur->pNextR3;
    }
    if (GCPtrCur > GCPtrBase + cb)
    {
        AssertMsgFailed(("cb (%#x) is less than the required range %#x!\n", cb, GCPtrCur - GCPtrBase));
        return VERR_PGM_MAPPINGS_FIX_TOO_SMALL;
    }

    /*
     * Loop the table assigning the mappings to the passed in memory
     * and call their relocator callback.
     */
    GCPtrCur = GCPtrBase;
    pCur = pVM->pgm.s.pMappingsR3;
    while (pCur)
    {
        unsigned iPDOld = pCur->GCPtr >> X86_PD_SHIFT;
        iPDNew = GCPtrCur >> X86_PD_SHIFT;

        /*
         * Relocate the page table(s).
         */
        pgmR3MapClearPDEs(pVM, pCur, iPDOld);
        pgmR3MapSetPDEs(pVM, pCur, iPDNew);

        /*
         * Update the entry.
         */
        pCur->GCPtr = GCPtrCur;
        pCur->GCPtrLast = GCPtrCur + pCur->cb - 1;

        /*
         * Callback to execute the relocation.
         */
        pCur->pfnRelocate(pVM, iPDOld << X86_PD_SHIFT, iPDNew << X86_PD_SHIFT, PGMRELOCATECALL_RELOCATE, pCur->pvUser);

        /*
         * Advance.
         */
        GCPtrCur += pCur->cb;
        pCur = pCur->pNextR3;
    }

#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
    /*
     * Turn off CR3 updating monitoring.
     */
    int rc2 = PGM_GST_PFN(UnmonitorCR3, pVM)(pVM);
    AssertRC(rc2);
#endif

    /*
     * Mark the mappings as fixed and return.
     */
    pVM->pgm.s.fMappingsFixed    = true;
    pVM->pgm.s.GCPtrMappingFixed = GCPtrBase;
    pVM->pgm.s.cbMappingFixed    = cb;
    pVM->pgm.s.fSyncFlags       &= ~PGM_SYNC_MONITOR_CR3;
    VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);
    return VINF_SUCCESS;
}


/**
 * Unfixes the mappings.
 * After calling this function mapping conflict detection will be enabled.
 *
 * @returns VBox status code.
 * @param   pVM         The VM.
 */
VMMR3DECL(int) PGMR3MappingsUnfix(PVM pVM)
{
    Log(("PGMR3MappingsUnfix: fMappingsFixed=%d\n", pVM->pgm.s.fMappingsFixed));

    /* Refuse in VT-x/AMD-V mode. */
    if (HWACCMR3IsActive(pVM))
        return VINF_SUCCESS;

    pVM->pgm.s.fMappingsFixed    = false;
    pVM->pgm.s.GCPtrMappingFixed = 0;
    pVM->pgm.s.cbMappingFixed    = 0;
    VM_FF_SET(pVM, VM_FF_PGM_SYNC_CR3);

    /*
     * Re-enable the CR3 monitoring.
     *
     * Paranoia: We flush the page pool before doing that because Windows
     * is using the CR3 page both as a PD and a PT, e.g. the pool may
     * be monitoring it.
     */
#ifdef PGMPOOL_WITH_MONITORING
    pgmPoolFlushAll(pVM);
#endif
    /* Remap CR3 as we have just flushed the CR3 shadow PML4 in case we're in long mode. */
    int rc = PGM_GST_PFN(MapCR3, pVM)(pVM, pVM->pgm.s.GCPhysCR3);
    AssertRCSuccess(rc);

#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
    rc = PGM_GST_PFN(MonitorCR3, pVM)(pVM, pVM->pgm.s.GCPhysCR3);
    AssertRCSuccess(rc);
#endif
    return VINF_SUCCESS;
}


/**
 * Map pages into the intermediate context (switcher code).
 * These pages are mapped at both the give virtual address and at
 * the physical address (for identity mapping).
 *
 * @returns VBox status code.
 * @param   pVM         The virtual machine.
 * @param   Addr        Intermediate context address of the mapping.
 * @param   HCPhys      Start of the range of physical pages. This must be entriely below 4GB!
 * @param   cbPages     Number of bytes to map.
 *
 * @remark  This API shall not be used to anything but mapping the switcher code.
 */
VMMR3DECL(int) PGMR3MapIntermediate(PVM pVM, RTUINTPTR Addr, RTHCPHYS HCPhys, unsigned cbPages)
{
    LogFlow(("PGMR3MapIntermediate: Addr=%RTptr HCPhys=%RHp cbPages=%#x\n", Addr, HCPhys, cbPages));

    /*
     * Adjust input.
     */
    cbPages += (uint32_t)HCPhys & PAGE_OFFSET_MASK;
    cbPages  = RT_ALIGN(cbPages, PAGE_SIZE);
    HCPhys  &= X86_PTE_PAE_PG_MASK;
    Addr    &= PAGE_BASE_MASK;
    /* We only care about the first 4GB, because on AMD64 we'll be repeating them all over the address space. */
    uint32_t uAddress = (uint32_t)Addr;

    /*
     * Assert input and state.
     */
    AssertMsg(pVM->pgm.s.offVM, ("Bad init order\n"));
    AssertMsg(pVM->pgm.s.pInterPD, ("Bad init order, paging.\n"));
    AssertMsg(cbPages <= (512 << PAGE_SHIFT), ("The mapping is too big %d bytes\n", cbPages));
    AssertMsg(HCPhys < _4G && HCPhys + cbPages < _4G, ("Addr=%RTptr HCPhys=%RHp cbPages=%d\n", Addr, HCPhys, cbPages));
    AssertReturn(!pVM->pgm.s.fFinalizedMappings, VERR_WRONG_ORDER);

    /*
     * Check for internal conflicts between the virtual address and the physical address.
     * A 1:1 mapping is fine, but partial overlapping is a no-no.
     */
    if (    uAddress != HCPhys
        &&  (   uAddress < HCPhys
                ? HCPhys - uAddress < cbPages
                : uAddress - HCPhys < cbPages
            )
       )
        AssertLogRelMsgFailedReturn(("Addr=%RTptr HCPhys=%RHp cbPages=%d\n", Addr, HCPhys, cbPages),
                                    VERR_PGM_INTERMEDIATE_PAGING_CONFLICT);

    const unsigned cPages = cbPages >> PAGE_SHIFT;
    int rc = pgmR3MapIntermediateCheckOne(pVM, uAddress, cPages, pVM->pgm.s.apInterPTs[0], pVM->pgm.s.apInterPaePTs[0]);
    if (RT_FAILURE(rc))
        return rc;
    rc = pgmR3MapIntermediateCheckOne(pVM, (uintptr_t)HCPhys, cPages, pVM->pgm.s.apInterPTs[1], pVM->pgm.s.apInterPaePTs[1]);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Everythings fine, do the mapping.
     */
    pgmR3MapIntermediateDoOne(pVM, uAddress, HCPhys, cPages, pVM->pgm.s.apInterPTs[0], pVM->pgm.s.apInterPaePTs[0]);
    pgmR3MapIntermediateDoOne(pVM, (uintptr_t)HCPhys, HCPhys, cPages, pVM->pgm.s.apInterPTs[1], pVM->pgm.s.apInterPaePTs[1]);

    return VINF_SUCCESS;
}


/**
 * Validates that there are no conflicts for this mapping into the intermediate context.
 *
 * @returns VBox status code.
 * @param   pVM         VM handle.
 * @param   uAddress    Address of the mapping.
 * @param   cPages      Number of pages.
 * @param   pPTDefault      Pointer to the default page table for this mapping.
 * @param   pPTPaeDefault   Pointer to the default page table for this mapping.
 */
static int pgmR3MapIntermediateCheckOne(PVM pVM, uintptr_t uAddress, unsigned cPages, PX86PT pPTDefault, PX86PTPAE pPTPaeDefault)
{
    AssertMsg((uAddress >> X86_PD_SHIFT) + cPages <= 1024, ("64-bit fixme\n"));

    /*
     * Check that the ranges are available.
     * (This code doesn't have to be fast.)
     */
    while (cPages > 0)
    {
        /*
         * 32-Bit.
         */
        unsigned iPDE = (uAddress >> X86_PD_SHIFT) & X86_PD_MASK;
        unsigned iPTE = (uAddress >> X86_PT_SHIFT) & X86_PT_MASK;
        PX86PT pPT = pPTDefault;
        if (pVM->pgm.s.pInterPD->a[iPDE].u)
        {
            RTHCPHYS HCPhysPT = pVM->pgm.s.pInterPD->a[iPDE].u & X86_PDE_PG_MASK;
            if (HCPhysPT == MMPage2Phys(pVM, pVM->pgm.s.apInterPTs[0]))
                pPT = pVM->pgm.s.apInterPTs[0];
            else if (HCPhysPT == MMPage2Phys(pVM, pVM->pgm.s.apInterPTs[1]))
                pPT = pVM->pgm.s.apInterPTs[1];
            else
            {
                /** @todo this must be handled with a relocation of the conflicting mapping!
                 * Which of course cannot be done because we're in the middle of the initialization. bad design! */
                AssertLogRelMsgFailedReturn(("Conflict between core code and PGMR3Mapping(). uAddress=%RHv\n", uAddress),
                                            VERR_PGM_INTERMEDIATE_PAGING_CONFLICT);
            }
        }
        if (pPT->a[iPTE].u)
            AssertLogRelMsgFailedReturn(("Conflict iPTE=%#x iPDE=%#x uAddress=%RHv pPT->a[iPTE].u=%RX32\n", iPTE, iPDE, uAddress, pPT->a[iPTE].u),
                                        VERR_PGM_INTERMEDIATE_PAGING_CONFLICT);

        /*
         * PAE.
         */
        const unsigned iPDPE= (uAddress >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE;
        iPDE = (uAddress >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK;
        iPTE = (uAddress >> X86_PT_PAE_SHIFT) & X86_PT_PAE_MASK;
        Assert(iPDPE < 4);
        Assert(pVM->pgm.s.apInterPaePDs[iPDPE]);
        PX86PTPAE pPTPae = pPTPaeDefault;
        if (pVM->pgm.s.apInterPaePDs[iPDPE]->a[iPDE].u)
        {
            RTHCPHYS HCPhysPT = pVM->pgm.s.apInterPaePDs[iPDPE]->a[iPDE].u & X86_PDE_PAE_PG_MASK;
            if (HCPhysPT == MMPage2Phys(pVM, pVM->pgm.s.apInterPaePTs[0]))
                pPTPae = pVM->pgm.s.apInterPaePTs[0];
            else if (HCPhysPT == MMPage2Phys(pVM, pVM->pgm.s.apInterPaePTs[0]))
                pPTPae = pVM->pgm.s.apInterPaePTs[1];
            else
            {
                /** @todo this must be handled with a relocation of the conflicting mapping!
                 * Which of course cannot be done because we're in the middle of the initialization. bad design! */
                AssertLogRelMsgFailedReturn(("Conflict between core code and PGMR3Mapping(). uAddress=%RHv\n", uAddress),
                                            VERR_PGM_INTERMEDIATE_PAGING_CONFLICT);
            }
        }
        if (pPTPae->a[iPTE].u)
            AssertLogRelMsgFailedReturn(("Conflict iPTE=%#x iPDE=%#x uAddress=%RHv pPTPae->a[iPTE].u=%#RX64\n", iPTE, iPDE, uAddress, pPTPae->a[iPTE].u),
                                        VERR_PGM_INTERMEDIATE_PAGING_CONFLICT);

        /* next */
        uAddress += PAGE_SIZE;
        cPages--;
    }

    return VINF_SUCCESS;
}



/**
 * Sets up the intermediate page tables for a verified mapping.
 *
 * @param   pVM             VM handle.
 * @param   uAddress        Address of the mapping.
 * @param   HCPhys          The physical address of the page range.
 * @param   cPages          Number of pages.
 * @param   pPTDefault      Pointer to the default page table for this mapping.
 * @param   pPTPaeDefault   Pointer to the default page table for this mapping.
 */
static void pgmR3MapIntermediateDoOne(PVM pVM, uintptr_t uAddress, RTHCPHYS HCPhys, unsigned cPages, PX86PT pPTDefault, PX86PTPAE pPTPaeDefault)
{
    while (cPages > 0)
    {
        /*
         * 32-Bit.
         */
        unsigned iPDE = (uAddress >> X86_PD_SHIFT) & X86_PD_MASK;
        unsigned iPTE = (uAddress >> X86_PT_SHIFT) & X86_PT_MASK;
        PX86PT pPT;
        if (pVM->pgm.s.pInterPD->a[iPDE].u)
            pPT = (PX86PT)MMPagePhys2Page(pVM, pVM->pgm.s.pInterPD->a[iPDE].u & X86_PDE_PG_MASK);
        else
        {
            pVM->pgm.s.pInterPD->a[iPDE].u = X86_PDE_P | X86_PDE_A | X86_PDE_RW
                                           | (uint32_t)MMPage2Phys(pVM, pPTDefault);
            pPT = pPTDefault;
        }
        pPT->a[iPTE].u = X86_PTE_P | X86_PTE_RW | X86_PTE_A | X86_PTE_D | (uint32_t)HCPhys;

        /*
         * PAE
         */
        const unsigned iPDPE= (uAddress >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE;
        iPDE = (uAddress >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK;
        iPTE = (uAddress >> X86_PT_PAE_SHIFT) & X86_PT_PAE_MASK;
        Assert(iPDPE < 4);
        Assert(pVM->pgm.s.apInterPaePDs[iPDPE]);
        PX86PTPAE pPTPae;
        if (pVM->pgm.s.apInterPaePDs[iPDPE]->a[iPDE].u)
            pPTPae = (PX86PTPAE)MMPagePhys2Page(pVM, pVM->pgm.s.apInterPaePDs[iPDPE]->a[iPDE].u & X86_PDE_PAE_PG_MASK);
        else
        {
            pPTPae = pPTPaeDefault;
            pVM->pgm.s.apInterPaePDs[iPDPE]->a[iPDE].u = X86_PDE_P | X86_PDE_A | X86_PDE_RW
                                                       | MMPage2Phys(pVM, pPTPaeDefault);
        }
        pPTPae->a[iPTE].u = X86_PTE_P | X86_PTE_RW | X86_PTE_A | X86_PTE_D | HCPhys;

        /* next */
        cPages--;
        HCPhys += PAGE_SIZE;
        uAddress += PAGE_SIZE;
    }
}


/**
 * Clears all PDEs involved with the mapping in the shadow and intermediate page tables.
 *
 * @param   pVM         The VM handle.
 * @param   pMap        Pointer to the mapping in question.
 * @param   iOldPDE     The index of the 32-bit PDE corresponding to the base of the mapping.
 */
static void pgmR3MapClearPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iOldPDE)
{
    unsigned i = pMap->cPTs;

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    pgmR3MapClearShadowPDEs(pVM, pMap, iOldPDE);
#endif

    iOldPDE += i;
    while (i-- > 0)
    {
        iOldPDE--;

        /*
         * 32-bit.
         */
        pVM->pgm.s.pInterPD->a[iOldPDE].u        = 0;
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        pVM->pgm.s.pShw32BitPdR3->a[iOldPDE].u   = 0;
#endif
        /*
         * PAE.
         */
        const unsigned iPD = iOldPDE / 256;         /* iOldPDE * 2 / 512; iOldPDE is in 4 MB pages */
        unsigned iPDE = iOldPDE * 2 % 512;
        pVM->pgm.s.apInterPaePDs[iPD]->a[iPDE].u = 0;
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        pVM->pgm.s.apShwPaePDsR3[iPD]->a[iPDE].u = 0;
#endif
        iPDE++;
        AssertFatal(iPDE < 512);
        pVM->pgm.s.apInterPaePDs[iPD]->a[iPDE].u = 0;
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        pVM->pgm.s.apShwPaePDsR3[iPD]->a[iPDE].u = 0;

        /* Clear the PGM_PDFLAGS_MAPPING flag for the page directory pointer entry. (legacy PAE guest mode) */
        pVM->pgm.s.pShwPaePdptR3->a[iPD].u &= ~PGM_PLXFLAGS_MAPPING;
#endif
    }
}


#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
/**
 * Clears all PDEs involved with the mapping in the shadow page table.
 *
 * @param   pVM         The VM handle.
 * @param   pMap        Pointer to the mapping in question.
 * @param   iOldPDE     The index of the 32-bit PDE corresponding to the base of the mapping.
 */
static void pgmR3MapClearShadowPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iOldPDE)
{
    unsigned i = pMap->cPTs;
    PGMMODE  enmShadowMode = PGMGetShadowMode(pVM);

    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
        return;

    iOldPDE += i;
    while (i-- > 0)
    {
        iOldPDE--;

        switch(enmShadowMode)
        {
        case PGMMODE_32_BIT:
        {
            PX86PD pShw32BitPd = pgmShwGet32BitPDPtr(&pVM->pgm.s);
            AssertFatal(pShw32BitPd);

            pShw32BitPd->a[iOldPDE].u   = 0;
            break;
        }

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
        {
            PX86PDPT  pPdpt = NULL;
            PX86PDPAE pShwPaePd = NULL;

            const unsigned iPD = iOldPDE / 256;         /* iOldPDE * 2 / 512; iOldPDE is in 4 MB pages */
            unsigned iPDE = iOldPDE * 2 % 512;
            pPdpt     = pgmShwGetPaePDPTPtr(&pVM->pgm.s);
            pShwPaePd = pgmShwGetPaePDPtr(&pVM->pgm.s, (iPD << X86_PDPT_SHIFT));
            AssertFatal(pShwPaePd);

            pShwPaePd->a[iPDE].u = 0;

            iPDE++;
            AssertFatal(iPDE < 512);

            pShwPaePd->a[iPDE].u = 0;
            /* Clear the PGM_PDFLAGS_MAPPING flag for the page directory pointer entry. (legacy PAE guest mode) */
            pPdpt->a[iPD].u &= ~PGM_PLXFLAGS_MAPPING;
            break;
        }
        }
    }
}
#endif

/**
 * Sets all PDEs involved with the mapping in the shadow and intermediate page tables.
 *
 * @param   pVM         The VM handle.
 * @param   pMap        Pointer to the mapping in question.
 * @param   iNewPDE     The index of the 32-bit PDE corresponding to the base of the mapping.
 */
static void pgmR3MapSetPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iNewPDE)
{
    PPGM pPGM = &pVM->pgm.s;

    Assert(!pgmMapAreMappingsEnabled(&pVM->pgm.s) || PGMGetGuestMode(pVM) <= PGMMODE_PAE_NX);

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
    pgmR3MapSetShadowPDEs(pVM, pMap, iNewPDE);
#endif

    /*
     * Init the page tables and insert them into the page directories.
     */
    unsigned i = pMap->cPTs;
    iNewPDE += i;
    while (i-- > 0)
    {
        iNewPDE--;

        /*
         * 32-bit.
         */
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        if (    pgmMapAreMappingsEnabled(&pVM->pgm.s)
            &&  pPGM->pShw32BitPdR3->a[iNewPDE].n.u1Present)
        {
            Assert(!(pPGM->pShw32BitPdR3->a[iNewPDE].u & PGM_PDFLAGS_MAPPING));
            pgmPoolFree(pVM, pPGM->pShw32BitPdR3->a[iNewPDE].u & X86_PDE_PG_MASK, PGMPOOL_IDX_PD, iNewPDE);
        }
#endif
        X86PDE Pde;
        /* Default mapping page directory flags are read/write and supervisor; individual page attributes determine the final flags */
        Pde.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | (uint32_t)pMap->aPTs[i].HCPhysPT;
        pPGM->pInterPD->a[iNewPDE]        = Pde;
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        if (pgmMapAreMappingsEnabled(&pVM->pgm.s))
            pPGM->pShw32BitPdR3->a[iNewPDE]   = Pde;
#endif
        /*
         * PAE.
         */
        const unsigned iPD = iNewPDE / 256;
        unsigned iPDE = iNewPDE * 2 % 512;
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        if (   pgmMapAreMappingsEnabled(&pVM->pgm.s)
            && pPGM->apShwPaePDsR3[iPD]->a[iPDE].n.u1Present)
        {
            Assert(!(pPGM->apShwPaePDsR3[iPD]->a[iPDE].u & PGM_PDFLAGS_MAPPING));
            pgmPoolFree(pVM, pPGM->apShwPaePDsR3[iPD]->a[iPDE].u & X86_PDE_PAE_PG_MASK, PGMPOOL_IDX_PAE_PD, iNewPDE * 2);
        }
#endif
        X86PDEPAE PdePae0;
        PdePae0.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT0;
        pPGM->apInterPaePDs[iPD]->a[iPDE] = PdePae0;
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        if (pgmMapAreMappingsEnabled(&pVM->pgm.s))
            pPGM->apShwPaePDsR3[iPD]->a[iPDE] = PdePae0;
#endif
        iPDE++;
        AssertFatal(iPDE < 512);
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        if (   pgmMapAreMappingsEnabled(&pVM->pgm.s)
            && pPGM->apShwPaePDsR3[iPD]->a[iPDE].n.u1Present)
        {
            Assert(!(pPGM->apShwPaePDsR3[iPD]->a[iPDE].u & PGM_PDFLAGS_MAPPING));
            pgmPoolFree(pVM, pPGM->apShwPaePDsR3[iPD]->a[iPDE].u & X86_PDE_PAE_PG_MASK, PGMPOOL_IDX_PAE_PD, iNewPDE * 2 + 1);
        }
#endif
        X86PDEPAE PdePae1;
        PdePae1.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT1;
        pPGM->apInterPaePDs[iPD]->a[iPDE] = PdePae1;
#ifndef VBOX_WITH_PGMPOOL_PAGING_ONLY
        if (pgmMapAreMappingsEnabled(&pVM->pgm.s))
        {
            pPGM->apShwPaePDsR3[iPD]->a[iPDE] = PdePae1;

            /* Set the PGM_PDFLAGS_MAPPING flag in the page directory pointer entry. (legacy PAE guest mode) */
            pPGM->pShwPaePdptR3->a[iPD].u |= PGM_PLXFLAGS_MAPPING;
        }
#endif
    }
}

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
/**
 * Sets all PDEs involved with the mapping in the shadow page table.
 *
 * @param   pVM         The VM handle.
 * @param   pMap        Pointer to the mapping in question.
 * @param   iNewPDE     The index of the 32-bit PDE corresponding to the base of the mapping.
 */
static void pgmR3MapSetShadowPDEs(PVM pVM, PPGMMAPPING pMap, unsigned iNewPDE)
{
    PPGM    pPGM = &pVM->pgm.s;
    PGMMODE enmShadowMode = PGMGetShadowMode(pVM);

    if (!pgmMapAreMappingsEnabled(&pVM->pgm.s))
        return;

    Assert(enmShadowMode <= PGMMODE_PAE_NX);

    /*
     * Init the page tables and insert them into the page directories.
     */
    unsigned i = pMap->cPTs;
    iNewPDE += i;
    while (i-- > 0)
    {
        iNewPDE--;

        switch(enmShadowMode)
        {
        case PGMMODE_32_BIT:
        {
            PX86PD pShw32BitPd = pgmShwGet32BitPDPtr(&pVM->pgm.s);
            AssertFatal(pShw32BitPd);

            if (pShw32BitPd->a[iNewPDE].n.u1Present)
            {
                Assert(!(pShw32BitPd->a[iNewPDE].u & PGM_PDFLAGS_MAPPING));
                pgmPoolFree(pVM, pShw32BitPd->a[iNewPDE].u & X86_PDE_PG_MASK, pVM->pgm.s.pShwPageCR3R3->idx, iNewPDE);
            }

            X86PDE Pde;
            /* Default mapping page directory flags are read/write and supervisor; individual page attributes determine the final flags */
            Pde.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | (uint32_t)pMap->aPTs[i].HCPhysPT;
            pShw32BitPd->a[iNewPDE]   = Pde;
            break;
        }

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
        {
            PX86PDPT  pShwPdpt;
            PX86PDPAE pShwPaePd;
            const unsigned iPdPt = iNewPDE / 256;
            unsigned iPDE = iNewPDE * 2 % 512;

            pShwPdpt  = pgmShwGetPaePDPTPtr(&pVM->pgm.s);
            Assert(pShwPdpt);
            pShwPaePd = pgmShwGetPaePDPtr(&pVM->pgm.s, (iPdPt << X86_PDPT_SHIFT));
            AssertFatal(pShwPaePd);

            PPGMPOOLPAGE pPoolPagePde = pgmPoolGetPageByHCPhys(pVM, pShwPdpt->a[iPdPt].u & X86_PDPE_PG_MASK);
            AssertFatal(pPoolPagePde);

            if (pShwPaePd->a[iPDE].n.u1Present)
            {
                Assert(!(pShwPaePd->a[iPDE].u & PGM_PDFLAGS_MAPPING));
                pgmPoolFree(pVM, pShwPaePd->a[iPDE].u & X86_PDE_PG_MASK, pPoolPagePde->idx, iNewPDE);
            }

            X86PDEPAE PdePae0;
            PdePae0.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT0;
            pShwPaePd->a[iPDE] = PdePae0;

            /* 2nd 2 MB PDE of the 4 MB region */
            iPDE++;
            AssertFatal(iPDE < 512);

            if (pShwPaePd->a[iPDE].n.u1Present)
            {
                Assert(!(pShwPaePd->a[iPDE].u & PGM_PDFLAGS_MAPPING));
                pgmPoolFree(pVM, pShwPaePd->a[iPDE].u & X86_PDE_PG_MASK, pPoolPagePde->idx, iNewPDE);
            }

            X86PDEPAE PdePae1;
            PdePae1.u = PGM_PDFLAGS_MAPPING | X86_PDE_P | X86_PDE_A | X86_PDE_RW | X86_PDE_US | pMap->aPTs[i].HCPhysPaePT1;
            pShwPaePd->a[iPDE] = PdePae1;

            /* Set the PGM_PDFLAGS_MAPPING flag in the page directory pointer entry. (legacy PAE guest mode) */
            pShwPdpt->a[iPdPt].u |= PGM_PLXFLAGS_MAPPING;
        }
        }
    }
}
#endif

/**
 * Relocates a mapping to a new address.
 *
 * @param   pVM                 VM handle.
 * @param   pMapping            The mapping to relocate.
 * @param   GCPtrOldMapping     The address of the start of the old mapping.
 * @param   GCPtrNewMapping     The address of the start of the new mapping.
 */
void pgmR3MapRelocate(PVM pVM, PPGMMAPPING pMapping, RTGCPTR GCPtrOldMapping, RTGCPTR GCPtrNewMapping)
{
    unsigned iPDOld = GCPtrOldMapping >> X86_PD_SHIFT;
    unsigned iPDNew = GCPtrNewMapping >> X86_PD_SHIFT;

    Log(("PGM: Relocating %s from %RGv to %RGv\n", pMapping->pszDesc, GCPtrOldMapping, GCPtrNewMapping));
    Assert(((unsigned)iPDOld << X86_PD_SHIFT) == pMapping->GCPtr);

    /*
     * Relocate the page table(s).
     */
    pgmR3MapClearPDEs(pVM, pMapping, iPDOld);
    pgmR3MapSetPDEs(pVM, pMapping, iPDNew);

    /*
     * Update and resort the mapping list.
     */

    /* Find previous mapping for pMapping, put result into pPrevMap. */
    PPGMMAPPING pPrevMap = NULL;
    PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3;
    while (pCur && pCur != pMapping)
    {
        /* next */
        pPrevMap = pCur;
        pCur = pCur->pNextR3;
    }
    Assert(pCur);

    /* Find mapping which >= than pMapping. */
    RTGCPTR     GCPtrNew = iPDNew << X86_PD_SHIFT;
    PPGMMAPPING pPrev = NULL;
    pCur = pVM->pgm.s.pMappingsR3;
    while (pCur && pCur->GCPtr < GCPtrNew)
    {
        /* next */
        pPrev = pCur;
        pCur = pCur->pNextR3;
    }

    if (pCur != pMapping && pPrev != pMapping)
    {
        /*
         * Unlink.
         */
        if (pPrevMap)
        {
            pPrevMap->pNextR3 = pMapping->pNextR3;
            pPrevMap->pNextRC = pMapping->pNextRC;
            pPrevMap->pNextR0 = pMapping->pNextR0;
        }
        else
        {
            pVM->pgm.s.pMappingsR3 = pMapping->pNextR3;
            pVM->pgm.s.pMappingsRC = pMapping->pNextRC;
            pVM->pgm.s.pMappingsR0 = pMapping->pNextR0;
        }

        /*
         * Link
         */
        pMapping->pNextR3 = pCur;
        if (pPrev)
        {
            pMapping->pNextRC = pPrev->pNextRC;
            pMapping->pNextR0 = pPrev->pNextR0;
            pPrev->pNextR3 = pMapping;
            pPrev->pNextRC = MMHyperR3ToRC(pVM, pMapping);
            pPrev->pNextR0 = MMHyperR3ToR0(pVM, pMapping);
        }
        else
        {
            pMapping->pNextRC = pVM->pgm.s.pMappingsRC;
            pMapping->pNextR0 = pVM->pgm.s.pMappingsR0;
            pVM->pgm.s.pMappingsR3 = pMapping;
            pVM->pgm.s.pMappingsRC = MMHyperR3ToRC(pVM, pMapping);
            pVM->pgm.s.pMappingsR0 = MMHyperR3ToR0(pVM, pMapping);
        }
    }

    /*
     * Update the entry.
     */
    pMapping->GCPtr = GCPtrNew;
    pMapping->GCPtrLast = GCPtrNew + pMapping->cb - 1;

    /*
     * Callback to execute the relocation.
     */
    pMapping->pfnRelocate(pVM, iPDOld << X86_PD_SHIFT, iPDNew << X86_PD_SHIFT, PGMRELOCATECALL_RELOCATE, pMapping->pvUser);
}


/**
 * Resolves a conflict between a page table based GC mapping and
 * the Guest OS page tables. (32 bits version)
 *
 * @returns VBox status code.
 * @param   pVM                 VM Handle.
 * @param   pMapping            The mapping which conflicts.
 * @param   pPDSrc              The page directory of the guest OS.
 * @param   GCPtrOldMapping     The address of the start of the current mapping.
 */
int pgmR3SyncPTResolveConflict(PVM pVM, PPGMMAPPING pMapping, PX86PD pPDSrc, RTGCPTR GCPtrOldMapping)
{
    STAM_PROFILE_START(&pVM->pgm.s.StatR3ResolveConflict, a);

    /*
     * Scan for free page directory entries.
     *
     * Note that we do not support mappings at the very end of the
     * address space since that will break our GCPtrEnd assumptions.
     */
    const unsigned  cPTs = pMapping->cPTs;
    unsigned        iPDNew = RT_ELEMENTS(pPDSrc->a) - cPTs; /* (+ 1 - 1) */
    while (iPDNew-- > 0)
    {
        if (pPDSrc->a[iPDNew].n.u1Present)
            continue;
        if (cPTs > 1)
        {
            bool fOk = true;
            for (unsigned i = 1; fOk && i < cPTs; i++)
                if (pPDSrc->a[iPDNew + i].n.u1Present)
                    fOk = false;
            if (!fOk)
                continue;
        }

        /*
         * Check that it's not conflicting with an intermediate page table mapping.
         */
        bool        fOk = true;
        unsigned    i   = cPTs;
        while (fOk && i-- > 0)
            fOk = !pVM->pgm.s.pInterPD->a[iPDNew + i].n.u1Present;
        if (!fOk)
            continue;
        /** @todo AMD64 should check the PAE directories and skip the 32bit stuff. */

        /*
         * Ask for the mapping.
         */
        RTGCPTR GCPtrNewMapping = iPDNew << X86_PD_SHIFT;

        if (pMapping->pfnRelocate(pVM, GCPtrOldMapping, GCPtrNewMapping, PGMRELOCATECALL_SUGGEST, pMapping->pvUser))
        {
            pgmR3MapRelocate(pVM, pMapping, GCPtrOldMapping, GCPtrNewMapping);
            STAM_PROFILE_STOP(&pVM->pgm.s.StatR3ResolveConflict, a);
            return VINF_SUCCESS;
        }
    }

    STAM_PROFILE_STOP(&pVM->pgm.s.StatR3ResolveConflict, a);
    AssertMsgFailed(("Failed to relocate page table mapping '%s' from %#x! (cPTs=%d)\n", pMapping->pszDesc, GCPtrOldMapping, cPTs));
    return VERR_PGM_NO_HYPERVISOR_ADDRESS;
}


/**
 * Resolves a conflict between a page table based GC mapping and
 * the Guest OS page tables. (PAE bits version)
 *
 * @returns VBox status code.
 * @param   pVM                 VM Handle.
 * @param   pMapping            The mapping which conflicts.
 * @param   GCPtrOldMapping     The address of the start of the current mapping.
 */
int pgmR3SyncPTResolveConflictPAE(PVM pVM, PPGMMAPPING pMapping, RTGCPTR GCPtrOldMapping)
{
    STAM_PROFILE_START(&pVM->pgm.s.StatR3ResolveConflict, a);

    for (int iPDPTE = X86_PG_PAE_PDPE_ENTRIES - 1; iPDPTE >= 0; iPDPTE--)
    {
        unsigned  iPDSrc;
        PX86PDPAE pPDSrc = pgmGstGetPaePDPtr(&pVM->pgm.s, (RTGCPTR32)iPDPTE << X86_PDPT_SHIFT, &iPDSrc, NULL);

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
        /* It would be annoying to have to deal with a PD that isn't (yet) present in the guest PDPT. */
        if (!pPDSrc)
            continue;
#endif

        /*
         * Scan for free page directory entries.
         *
         * Note that we do not support mappings at the very end of the
         * address space since that will break our GCPtrEnd assumptions.
         * Nor do we support mappings crossing page directories.
         */
        const unsigned  cPTs = pMapping->cb >> X86_PD_PAE_SHIFT;
        unsigned        iPDNew = RT_ELEMENTS(pPDSrc->a) - cPTs; /* (+ 1 - 1) */

        while (iPDNew-- > 0)
        {
            /* Ugly assumption that mappings start on a 4 MB boundary. */
            if (iPDNew & 1)
                continue;

            if (pPDSrc)
            {
                if (pPDSrc->a[iPDNew].n.u1Present)
                    continue;
                if (cPTs > 1)
                {
                    bool fOk = true;
                    for (unsigned i = 1; fOk && i < cPTs; i++)
                        if (pPDSrc->a[iPDNew + i].n.u1Present)
                            fOk = false;
                    if (!fOk)
                        continue;
                }
            }
            /*
             * Check that it's not conflicting with an intermediate page table mapping.
             */
            bool        fOk = true;
            unsigned    i   = cPTs;
            while (fOk && i-- > 0)
                fOk = !pVM->pgm.s.apInterPaePDs[iPDPTE]->a[iPDNew + i].n.u1Present;
            if (!fOk)
                continue;

            /*
             * Ask for the mapping.
             */
            RTGCPTR GCPtrNewMapping = ((RTGCPTR32)iPDPTE << X86_PDPT_SHIFT) + (iPDNew << X86_PD_PAE_SHIFT);

            if (pMapping->pfnRelocate(pVM, GCPtrOldMapping, GCPtrNewMapping, PGMRELOCATECALL_SUGGEST, pMapping->pvUser))
            {
                pgmR3MapRelocate(pVM, pMapping, GCPtrOldMapping, GCPtrNewMapping);
                STAM_PROFILE_STOP(&pVM->pgm.s.StatR3ResolveConflict, a);
                return VINF_SUCCESS;
            }
        }
    }
    STAM_PROFILE_STOP(&pVM->pgm.s.StatR3ResolveConflict, a);
    AssertMsgFailed(("Failed to relocate page table mapping '%s' from %#x! (cPTs=%d)\n", pMapping->pszDesc, GCPtrOldMapping, pMapping->cb >> X86_PD_PAE_SHIFT));
    return VERR_PGM_NO_HYPERVISOR_ADDRESS;
}


/**
 * Checks guest PD for conflicts with VMM GC mappings.
 *
 * @returns true if conflict detected.
 * @returns false if not.
 * @param   pVM         The virtual machine.
 * @param   cr3         Guest context CR3 register.
 * @param   fRawR0      Whether RawR0 is enabled or not.
 */
VMMR3DECL(bool) PGMR3MapHasConflicts(PVM pVM, uint64_t cr3, bool fRawR0) /** @todo how many HasConflict constructs do we really need? */
{
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
        return false;

    PGMMODE const enmGuestMode = PGMGetGuestMode(pVM);
    Assert(enmGuestMode <= PGMMODE_PAE_NX);

    /*
     * Iterate mappings.
     */
    if (enmGuestMode == PGMMODE_32_BIT)
    {
        /*
         * Resolve the page directory.
         */
        PX86PD pPD = pVM->pgm.s.pGst32BitPdR3;
        Assert(pPD);
        Assert(pPD == (PX86PD)PGMPhysGCPhys2R3PtrAssert(pVM, cr3 & X86_CR3_PAGE_MASK, sizeof(*pPD)));

        for (PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3; pCur; pCur = pCur->pNextR3)
        {
            unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;
            unsigned iPT = pCur->cPTs;
            while (iPT-- > 0)
                if (    pPD->a[iPDE + iPT].n.u1Present /** @todo PGMGstGetPDE. */
                    &&  (fRawR0 || pPD->a[iPDE + iPT].n.u1User))
                {
                    STAM_COUNTER_INC(&pVM->pgm.s.StatR3DetectedConflicts);
                    Log(("PGMR3HasMappingConflicts: Conflict was detected at %08RX32 for mapping %s (32 bits)\n"
                         "                          iPDE=%#x iPT=%#x PDE=%RGp.\n",
                        (iPT + iPDE) << X86_PD_SHIFT, pCur->pszDesc,
                        iPDE, iPT, pPD->a[iPDE + iPT].au32[0]));
                    return true;
                }
        }
    }
    else if (   enmGuestMode == PGMMODE_PAE
             || enmGuestMode == PGMMODE_PAE_NX)
    {
        for (PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3; pCur; pCur = pCur->pNextR3)
        {
            RTGCPTR   GCPtr = pCur->GCPtr;

            unsigned  iPT = pCur->cb >> X86_PD_PAE_SHIFT;
            while (iPT-- > 0)
            {
                X86PDEPAE Pde = pgmGstGetPaePDE(&pVM->pgm.s, GCPtr);

                if (   Pde.n.u1Present
                    && (fRawR0 || Pde.n.u1User))
                {
                    STAM_COUNTER_INC(&pVM->pgm.s.StatR3DetectedConflicts);
                    Log(("PGMR3HasMappingConflicts: Conflict was detected at %RGv for mapping %s (PAE)\n"
                         "                          PDE=%016RX64.\n",
                        GCPtr, pCur->pszDesc, Pde.u));
                    return true;
                }
                GCPtr += (1 << X86_PD_PAE_SHIFT);
            }
        }
    }
    else
        AssertFailed();

    return false;
}

#ifdef VBOX_WITH_PGMPOOL_PAGING_ONLY
/**
 * Apply the hypervisor mappings to the active CR3.
 *
 * @returns VBox status.
 * @param   pVM         The virtual machine.
 */
VMMR3DECL(int) PGMR3MapActivate(PVM pVM)
{
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
        return VINF_SUCCESS;

    /*
     * Iterate mappings.
     */
    for (PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3; pCur; pCur = pCur->pNextR3)
    {
        unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;

        pgmR3MapSetShadowPDEs(pVM, pCur, iPDE);
    }

    return VINF_SUCCESS;
}

/**
 * Remove the hypervisor mappings from the active CR3
 *
 * @returns VBox status.
 * @param   pVM         The virtual machine.
 */
VMMR3DECL(int) PGMR3MapDeactivate(PVM pVM)
{
    /*
     * Can skip this if mappings are safely fixed.
     */
    if (pVM->pgm.s.fMappingsFixed)
        return VINF_SUCCESS;

    /*
     * Iterate mappings.
     */
    for (PPGMMAPPING pCur = pVM->pgm.s.pMappingsR3; pCur; pCur = pCur->pNextR3)
    {
        unsigned iPDE = pCur->GCPtr >> X86_PD_SHIFT;

        pgmR3MapClearShadowPDEs(pVM, pCur, iPDE);
    }
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_PGMPOOL_PAGING_ONLY */

/**
 * Read memory from the guest mappings.
 *
 * This will use the page tables associated with the mappings to
 * read the memory. This means that not all kind of memory is readable
 * since we don't necessarily know how to convert that physical address
 * to a HC virtual one.
 *
 * @returns VBox status.
 * @param   pVM         VM handle.
 * @param   pvDst       The destination address (HC of course).
 * @param   GCPtrSrc    The source address (GC virtual address).
 * @param   cb          Number of bytes to read.
 *
 * @remarks The is indirectly for DBGF only.
 * @todo    Consider renaming it to indicate it's special usage, or just
 *          reimplement it in MMR3HyperReadGCVirt.
 */
VMMR3DECL(int) PGMR3MapRead(PVM pVM, void *pvDst, RTGCPTR GCPtrSrc, size_t cb)
{
    /*
     * Simplicity over speed... Chop the request up into chunks
     * which don't cross pages.
     */
    if (cb + (GCPtrSrc & PAGE_OFFSET_MASK) > PAGE_SIZE)
    {
        for (;;)
        {
            size_t cbRead = RT_MIN(cb, PAGE_SIZE - (GCPtrSrc & PAGE_OFFSET_MASK));
            int rc = PGMR3MapRead(pVM, pvDst, GCPtrSrc, cbRead);
            if (RT_FAILURE(rc))
                return rc;
            cb -= cbRead;
            if (!cb)
                break;
            pvDst = (char *)pvDst + cbRead;
            GCPtrSrc += cbRead;
        }
        return VINF_SUCCESS;
    }

    /*
     * Find the mapping.
     */
    PPGMMAPPING pCur = pVM->pgm.s.CTX_SUFF(pMappings);
    while (pCur)
    {
        RTGCPTR off = GCPtrSrc - pCur->GCPtr;
        if (off < pCur->cb)
        {
            if (off + cb > pCur->cb)
            {
                AssertMsgFailed(("Invalid page range %RGv LB%#x. mapping '%s' %RGv to %RGv\n",
                                 GCPtrSrc, cb, pCur->pszDesc, pCur->GCPtr, pCur->GCPtrLast));
                return VERR_INVALID_PARAMETER;
            }

            unsigned iPT  = off >> X86_PD_SHIFT;
            unsigned iPTE = (off >> PAGE_SHIFT) & X86_PT_MASK;
            while (cb > 0 && iPTE < RT_ELEMENTS(CTXALLSUFF(pCur->aPTs[iPT].pPT)->a))
            {
                if (!CTXALLSUFF(pCur->aPTs[iPT].paPaePTs)[iPTE / 512].a[iPTE % 512].n.u1Present)
                    return VERR_PAGE_NOT_PRESENT;
                RTHCPHYS HCPhys = CTXALLSUFF(pCur->aPTs[iPT].paPaePTs)[iPTE / 512].a[iPTE % 512].u & X86_PTE_PAE_PG_MASK;

                /*
                 * Get the virtual page from the physical one.
                 */
                void *pvPage;
                int rc = MMR3HCPhys2HCVirt(pVM, HCPhys, &pvPage);
                if (RT_FAILURE(rc))
                    return rc;

                memcpy(pvDst, (char *)pvPage + (GCPtrSrc & PAGE_OFFSET_MASK), cb);
                return VINF_SUCCESS;
            }
        }

        /* next */
        pCur = CTXALLSUFF(pCur->pNext);
    }

    return VERR_INVALID_POINTER;
}


/**
 * Info callback for 'pgmhandlers'.
 *
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The arguments. phys or virt.
 */
DECLCALLBACK(void) pgmR3MapInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    pHlp->pfnPrintf(pHlp, pVM->pgm.s.fMappingsFixed
                    ? "\nThe mappings are FIXED.\n"
                    : "\nThe mappings are FLOATING.\n");
    PPGMMAPPING pCur;
    for (pCur = pVM->pgm.s.pMappingsR3; pCur; pCur = pCur->pNextR3)
        pHlp->pfnPrintf(pHlp, "%RGv - %RGv  %s\n", pCur->GCPtr, pCur->GCPtrLast, pCur->pszDesc);
}

