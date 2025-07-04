/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TLB Exception Handling for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Vineetg: April 2011 :
 *  -MMU v1: moved out legacy code into a separate file
 *  -MMU v3: PD{0,1} bits layout changed: They don't overlap anymore,
 *      helps avoid a shift when preparing PD0 from PTE
 *
 * Vineetg: July 2009
 *  -For MMU V2, we need not do heuristics at the time of committing a D-TLB
 *   entry, so that it doesn't knock out its I-TLB entry
 *  -Some more fine tuning:
 *   bmsk instead of add, asl.cc instead of branch, delay slot utilise etc
 *
 * Vineetg: July 2009
 *  -Practically rewrote the I/D TLB Miss handlers
 *   Now 40 and 135 instructions apiece as compared to 131 and 449 resp.
 *   Hence Leaner by 1.5 K
 *   Used Conditional arithmetic to replace excessive branching
 *   Also used short instructions wherever possible
 *
 * Vineetg: Aug 13th 2008
 *  -Passing ECR (Exception Cause REG) to do_page_fault( ) for printing
 *   more information in case of a Fatality
 *
 * Vineetg: March 25th Bug #92690
 *  -Added Debug Code to check if sw-ASID == hw-ASID

 * Rahul Trivedi, Amit Bhor: Codito Technologies 2004
 */

#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <asm/entry.h>
#include <asm/mmu.h>
#include <asm/arcregs.h>
#include <asm/cache.h>
#include <asm/processor.h>

#ifdef CONFIG_ISA_ARCOMPACT
;-----------------------------------------------------------------
; ARC700 Exception Handling doesn't auto-switch stack and it only provides
; ONE scratch AUX reg "ARC_REG_SCRATCH_DATA0"
;
; For Non-SMP, the scratch AUX reg is repurposed to cache task PGD, so a
; "global" is used to free-up FIRST core reg to be able to code the rest of
; exception prologue (IRQ auto-disabled on Exceptions, so it's IRQ-safe).
; Since the Fast Path TLB Miss handler is coded with 4 regs, the remaining 3
; need to be saved as well by extending the "global" to be 4 words. Hence
;	".size   ex_saved_reg1, 16"
; [All of this dance is to avoid stack switching for each TLB Miss, since we
; only need to save only a handful of regs, as opposed to complete reg file]
;
; For ARC700 SMP, the "global" obviously can't be used for free up the FIRST
; core reg as it will not be SMP safe.
; Thus scratch AUX reg is used (and no longer used to cache task PGD).
; To save the rest of 3 regs - per cpu, the global is made "per-cpu".
; Epilogue thus has to locate the "per-cpu" storage for regs.
; To avoid cache line bouncing the per-cpu global is aligned/sized per
; L1_CACHE_SHIFT, despite fundamentally needing to be 12 bytes only. Hence
;	".size   ex_saved_reg1, (CONFIG_NR_CPUS << L1_CACHE_SHIFT)"

; As simple as that....
;--------------------------------------------------------------------------

; scratch memory to save [r0-r3] used to code TLB refill Handler
ARCFP_DATA ex_saved_reg1
	.align 1 << L1_CACHE_SHIFT
	.type   ex_saved_reg1, @object
#ifdef CONFIG_SMP
	.size   ex_saved_reg1, (CONFIG_NR_CPUS << L1_CACHE_SHIFT)
ex_saved_reg1:
	.zero (CONFIG_NR_CPUS << L1_CACHE_SHIFT)
#else
	.size   ex_saved_reg1, 16
ex_saved_reg1:
	.zero 16
#endif

.macro TLBMISS_FREEUP_REGS
#ifdef CONFIG_SMP
	sr  r0, [ARC_REG_SCRATCH_DATA0]	; freeup r0 to code with
	GET_CPU_ID  r0			; get to per cpu scratch mem,
	asl r0, r0, L1_CACHE_SHIFT	; cache line wide per cpu
	add r0, @ex_saved_reg1, r0
#else
	st    r0, [@ex_saved_reg1]
	mov_s r0, @ex_saved_reg1
#endif
	st_s  r1, [r0, 4]
	st_s  r2, [r0, 8]
	st_s  r3, [r0, 12]
.endm

.macro TLBMISS_RESTORE_REGS
#ifdef CONFIG_SMP
	GET_CPU_ID  r0			; get to per cpu scratch mem
	asl r0, r0, L1_CACHE_SHIFT	; each is cache line wide
	add r0, @ex_saved_reg1, r0
	ld_s  r3, [r0,12]
	ld_s  r2, [r0, 8]
	ld_s  r1, [r0, 4]
	lr    r0, [ARC_REG_SCRATCH_DATA0]
#else
	mov_s r0, @ex_saved_reg1
	ld_s  r3, [r0,12]
	ld_s  r2, [r0, 8]
	ld_s  r1, [r0, 4]
	ld_s  r0, [r0]
#endif
.endm

#else	/* ARCv2 */

.macro TLBMISS_FREEUP_REGS
#ifdef CONFIG_ARC_HAS_LL64
	std   r0, [sp, -16]
	std   r2, [sp, -8]
#else
	PUSH  r0
	PUSH  r1
	PUSH  r2
	PUSH  r3
#endif
.endm

.macro TLBMISS_RESTORE_REGS
#ifdef CONFIG_ARC_HAS_LL64
	ldd   r0, [sp, -16]
	ldd   r2, [sp, -8]
#else
	POP   r3
	POP   r2
	POP   r1
	POP   r0
#endif
.endm

#endif

;============================================================================
;TLB Miss handling Code
;============================================================================

#ifndef PMD_SHIFT
#define PMD_SHIFT PUD_SHIFT
#endif

#ifndef PUD_SHIFT
#define PUD_SHIFT PGDIR_SHIFT
#endif

;-----------------------------------------------------------------------------
; This macro does the page-table lookup for the faulting address.
; OUT: r0 = PTE faulted on, r1 = ptr to PTE, r2 = Faulting V-address
.macro LOAD_FAULT_PTE

	lr  r2, [efa]

#ifdef CONFIG_ISA_ARCV2
	lr  r1, [ARC_REG_SCRATCH_DATA0] ; current pgd
#else
	GET_CURR_TASK_ON_CPU  r1
	ld  r1, [r1, TASK_ACT_MM]
	ld  r1, [r1, MM_PGD]
#endif

	lsr     r0, r2, PGDIR_SHIFT     ; Bits for indexing into PGD
	ld.as   r3, [r1, r0]            ; PGD entry corresp to faulting addr
	tst	r3, r3
	bz	do_slow_path_pf         ; if no Page Table, do page fault

#if CONFIG_PGTABLE_LEVELS > 3
	lsr     r0, r2, PUD_SHIFT	; Bits for indexing into PUD
	and	r0, r0, (PTRS_PER_PUD - 1)
	ld.as	r1, [r3, r0]		; PMD entry
	tst	r1, r1
	bz	do_slow_path_pf
	mov	r3, r1
#endif

#if CONFIG_PGTABLE_LEVELS > 2
	lsr     r0, r2, PMD_SHIFT	; Bits for indexing into PMD
	and	r0, r0, (PTRS_PER_PMD - 1)
	ld.as	r1, [r3, r0]		; PMD entry
	tst	r1, r1
	bz	do_slow_path_pf
	mov	r3, r1
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	and.f	0, r3, _PAGE_HW_SZ	; Is this Huge PMD (thp)
	add2.nz	r1, r1, r0
	bnz.d	2f		; YES: PGD == PMD has THP PTE: stop pgd walk
	mov.nz	r0, r3

#endif
	and	r1, r3, PAGE_MASK

	; Get the PTE entry: The idea is
	; (1) x = addr >> PAGE_SHIFT 	-> masks page-off bits from @fault-addr
	; (2) y = x & (PTRS_PER_PTE - 1) -> to get index
	; (3) z = (pgtbl + y * 4)

#ifdef CONFIG_ARC_HAS_PAE40
#define PTE_SIZE_LOG	3	/* 8 == 2 ^ 3 */
#else
#define PTE_SIZE_LOG	2	/* 4 == 2 ^ 2 */
#endif

	; multiply in step (3) above avoided by shifting lesser in step (1)
	lsr     r0, r2, ( PAGE_SHIFT - PTE_SIZE_LOG )
	and     r0, r0, ( (PTRS_PER_PTE - 1) << PTE_SIZE_LOG )
	ld.aw   r0, [r1, r0]            ; r0: PTE (lower word only for PAE40)
					; r1: PTE ptr

2:

.endm

;-----------------------------------------------------------------
; Convert Linux PTE entry into TLB entry
; A one-word PTE entry is programmed as two-word TLB Entry [PD0:PD1] in mmu
;    (for PAE40, two-words PTE, while three-word TLB Entry [PD0:PD1:PD1HI])
; IN: r0 = PTE, r1 = ptr to PTE

.macro CONV_PTE_TO_TLB
	and    r3, r0, PTE_BITS_RWX	;          r  w  x
	asl    r2, r3, 3		; Kr Kw Kx 0  0  0 (GLOBAL, kernel only)
	and.f  0,  r0, _PAGE_GLOBAL
	or.z   r2, r2, r3		; Kr Kw Kx Ur Uw Ux (!GLOBAL, user page)

	and r3, r0, PTE_BITS_NON_RWX_IN_PD1 ; Extract PFN+cache bits from PTE
	or  r3, r3, r2

	sr  r3, [ARC_REG_TLBPD1]    	; paddr[31..13] | Kr Kw Kx Ur Uw Ux | C
#ifdef	CONFIG_ARC_HAS_PAE40
	ld	r3, [r1, 4]		; paddr[39..32]
	sr	r3, [ARC_REG_TLBPD1HI]
#endif

	and r2, r0, PTE_BITS_IN_PD0 ; Extract other PTE flags: (V)alid, (G)lb

	lr  r3,[ARC_REG_TLBPD0]     ; MMU prepares PD0 with vaddr and asid

	or  r3, r3, r2              ; S | vaddr | {sasid|asid}
	sr  r3,[ARC_REG_TLBPD0]     ; rewrite PD0
.endm

;-----------------------------------------------------------------
; Commit the TLB entry into MMU

.macro COMMIT_ENTRY_TO_MMU
#ifdef CONFIG_ARC_MMU_V3

	/* Get free TLB slot: Set = computed from vaddr, way = random */
	sr  TLBGetIndex, [ARC_REG_TLBCOMMAND]

	/* Commit the Write */
	sr TLBWriteNI, [ARC_REG_TLBCOMMAND]

#else
	sr TLBInsertEntry, [ARC_REG_TLBCOMMAND]
#endif

88:
.endm


ARCFP_CODE	;Fast Path Code, candidate for ICCM

;-----------------------------------------------------------------------------
; I-TLB Miss Exception Handler
;-----------------------------------------------------------------------------

ENTRY(EV_TLBMissI)

	TLBMISS_FREEUP_REGS

	;----------------------------------------------------------------
	; Get the PTE corresponding to V-addr accessed, r2 is setup with EFA
	LOAD_FAULT_PTE

	;----------------------------------------------------------------
	; VERIFY_PTE: Check if PTE permissions approp for executing code
	cmp_s   r2, VMALLOC_START
	mov_s   r2, (_PAGE_PRESENT | _PAGE_EXECUTE)
	or.hs   r2, r2, _PAGE_GLOBAL

	and     r3, r0, r2  ; Mask out NON Flag bits from PTE
	xor.f   r3, r3, r2  ; check ( ( pte & flags_test ) == flags_test )
	bnz     do_slow_path_pf

	; Let Linux VM know that the page was accessed
	or      r0, r0, _PAGE_ACCESSED  ; set Accessed Bit
	st_s    r0, [r1]                ; Write back PTE

	CONV_PTE_TO_TLB
	COMMIT_ENTRY_TO_MMU
	TLBMISS_RESTORE_REGS
EV_TLBMissI_fast_ret:	; additional label for VDK OS-kit instrumentation
	rtie

END(EV_TLBMissI)

;-----------------------------------------------------------------------------
; D-TLB Miss Exception Handler
;-----------------------------------------------------------------------------

ENTRY(EV_TLBMissD)

	TLBMISS_FREEUP_REGS

	;----------------------------------------------------------------
	; Get the PTE corresponding to V-addr accessed
	; If PTE exists, it will setup, r0 = PTE, r1 = Ptr to PTE, r2 = EFA
	LOAD_FAULT_PTE

	;----------------------------------------------------------------
	; VERIFY_PTE: Chk if PTE permissions approp for data access (R/W/R+W)

	cmp_s	r2, VMALLOC_START
	mov_s   r2, _PAGE_PRESENT	; common bit for K/U PTE
	or.hs	r2, r2, _PAGE_GLOBAL	; kernel PTE only

	; Linux PTE [RWX] bits are semantically overloaded:
	; -If PAGE_GLOBAL set, they refer to kernel-only flags (vmalloc)
	; -Otherwise they are user-mode permissions, and those are exactly
	;  same for kernel mode as well (e.g. copy_(to|from)_user)

	lr      r3, [ecr]
	btst_s  r3, ECR_C_BIT_DTLB_LD_MISS	; Read Access
	or.nz   r2, r2, _PAGE_READ      	; chk for Read flag in PTE
	btst_s  r3, ECR_C_BIT_DTLB_ST_MISS	; Write Access
	or.nz   r2, r2, _PAGE_WRITE     	; chk for Write flag in PTE
	; Above laddering takes care of XCHG access (both R and W)

	; By now, r2 setup with all the Flags we need to check in PTE
	and     r3, r0, r2              ; Mask out NON Flag bits from PTE
	brne.d  r3, r2, do_slow_path_pf ; is ((pte & flags_test) == flags_test)

	;----------------------------------------------------------------
	; UPDATE_PTE: Let Linux VM know that page was accessed/dirty
	or      r0, r0, _PAGE_ACCESSED        ; Accessed bit always
	or.nz   r0, r0, _PAGE_DIRTY           ; if Write, set Dirty bit as well
	st_s    r0, [r1]                      ; Write back PTE

	CONV_PTE_TO_TLB

	COMMIT_ENTRY_TO_MMU
	TLBMISS_RESTORE_REGS
EV_TLBMissD_fast_ret:	; additional label for VDK OS-kit instrumentation
	rtie

;-------- Common routine to call Linux Page Fault Handler -----------
do_slow_path_pf:

#ifdef CONFIG_ISA_ARCV2
	; Set Z flag if exception in U mode. Hardware micro-ops do this on any
	; taken interrupt/exception, and thus is already the case at the entry
	; above, but ensuing code would have already clobbered.
	; EXCEPTION_PROLOGUE called in slow path, relies on correct Z flag set

	lr	r2, [erstatus]
	and	r2, r2, STATUS_U_MASK
	bxor.f	0, r2, STATUS_U_BIT
#endif

	; Restore the 4-scratch regs saved by fast path miss handler
	TLBMISS_RESTORE_REGS

	; Slow path TLB Miss handled as a regular ARC Exception
	; (stack switching / save the complete reg-file).
	b  call_do_page_fault
END(EV_TLBMissD)
