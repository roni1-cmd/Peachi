/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Vineetg: March 2009 (Supporting 2 levels of Interrupts)
 *  Stack switching code can no longer reliably rely on the fact that
 *  if we are NOT in user mode, stack is switched to kernel mode.
 *  e.g. L2 IRQ interrupted a L1 ISR which had not yet completed
 *  its prologue including stack switching from user mode
 *
 * Vineetg: Aug 28th 2008: Bug #94984
 *  -Zero Overhead Loop Context shd be cleared when entering IRQ/EXcp/Trap
 *   Normally CPU does this automatically, however when doing FAKE rtie,
 *   we also need to explicitly do this. The problem in macros
 *   FAKE_RET_FROM_EXCPN and FAKE_RET_FROM_EXCPN_LOCK_IRQ was that this bit
 *   was being "CLEARED" rather then "SET". Actually "SET" clears ZOL context
 *
 * Vineetg: May 5th 2008
 *  -Modified CALLEE_REG save/restore macros to handle the fact that
 *      r25 contains the kernel current task ptr
 *  - Defined Stack Switching Macro to be reused in all intr/excp hdlrs
 *  - Shaved off 11 instructions from RESTORE_ALL_INT1 by using the
 *      address Write back load ld.ab instead of separate ld/add instn
 *
 * Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef __ASM_ARC_ENTRY_COMPACT_H
#define __ASM_ARC_ENTRY_COMPACT_H

#include <asm/asm-offsets.h>
#include <asm/irqflags-compact.h>
#include <asm/thread_info.h>	/* For THREAD_SIZE */

/* Note on the LD/ST addr modes with addr reg wback
 *
 * LD.a same as LD.aw
 *
 * LD.a    reg1, [reg2, x]  => Pre Incr
 *      Eff Addr for load = [reg2 + x]
 *
 * LD.ab   reg1, [reg2, x]  => Post Incr
 *      Eff Addr for load = [reg2]
 */

.macro PUSHAX aux
	lr	r9, [\aux]
	push	r9
.endm

.macro POPAX aux
	pop	r9
	sr	r9, [\aux]
.endm

.macro  SAVE_R0_TO_R12
	push	r0
	push	r1
	push	r2
	push	r3
	push	r4
	push	r5
	push	r6
	push	r7
	push	r8
	push	r9
	push	r10
	push	r11
	push	r12
.endm

.macro RESTORE_R12_TO_R0
	pop	r12
	pop	r11
	pop	r10
	pop	r9
	pop	r8
	pop	r7
	pop	r6
	pop	r5
	pop	r4
	pop	r3
	pop	r2
	pop	r1
	pop	r0
.endm

.macro SAVE_ABI_CALLEE_REGS
	push	r13
	push	r14
	push	r15
	push	r16
	push	r17
	push	r18
	push	r19
	push	r20
	push	r21
	push	r22
	push	r23
	push	r24
	push	r25
.endm

.macro RESTORE_ABI_CALLEE_REGS
	pop	r25
	pop	r24
	pop	r23
	pop	r22
	pop	r21
	pop	r20
	pop	r19
	pop	r18
	pop	r17
	pop	r16
	pop	r15
	pop	r14
	pop	r13
.endm

/*--------------------------------------------------------------
 * Switch to Kernel Mode stack if SP points to User Mode stack
 *
 * Entry   : r9 contains pre-IRQ/exception/trap status32
 * Exit    : SP set to K mode stack
 *           SP at the time of entry (K/U) saved @ pt_regs->sp
 * Clobbers: r9
 *-------------------------------------------------------------*/

.macro SWITCH_TO_KERNEL_STK

	/* User Mode when this happened ? Yes: Proceed to switch stack */
	bbit1   r9, STATUS_U_BIT, 88f

	/* OK we were already in kernel mode when this event happened, thus can
	 * assume SP is kernel mode SP. _NO_ need to do any stack switching
	 */

#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS
	/* However....
	 * If Level 2 Interrupts enabled, we may end up with a corner case:
	 * 1. User Task executing
	 * 2. L1 IRQ taken, ISR starts (CPU auto-switched to KERNEL mode)
	 * 3. But before it could switch SP from USER to KERNEL stack
	 *      a L2 IRQ "Interrupts" L1
	 * That way although L2 IRQ happened in Kernel mode, stack is still
	 * not switched.
	 * To handle this, we may need to switch stack even if in kernel mode
	 * provided SP has values in range of USER mode stack ( < 0x7000_0000 )
	 */
	brlo sp, VMALLOC_START, 88f

	/* TODO: vineetg:
	 * We need to be a bit more cautious here. What if a kernel bug in
	 * L1 ISR, caused SP to go whaco (some small value which looks like
	 * USER stk) and then we take L2 ISR.
	 * Above brlo alone would treat it as a valid L1-L2 scenario
	 * instead of shouting around
	 * The only feasible way is to make sure this L2 happened in
	 * L1 prelogue ONLY i.e. ilink2 is less than a pre-set marker in
	 * L1 ISR before it switches stack
	 */

#endif

    /*------Intr/Ecxp happened in kernel mode, SP already setup ------ */
	/* save it nevertheless @ pt_regs->sp for uniformity */

	b.d	66f
	st	sp, [sp, PT_sp - SZ_PT_REGS]

88: /*------Intr/Ecxp happened in user mode, "switch" stack ------ */

	GET_CURR_TASK_ON_CPU   r9

	/* With current tsk in r9, get its kernel mode stack base */
	GET_TSK_STACK_BASE  r9, r9

	/* save U mode SP @ pt_regs->sp */
	st	sp, [r9, PT_sp - SZ_PT_REGS]

	/* final SP switch */
	mov	sp, r9
66:
.endm

/*------------------------------------------------------------
 * "FAKE" a rtie to return from CPU Exception context
 * This is to re-enable Exceptions within exception
 * Look at EV_ProtV to see how this is actually used
 *-------------------------------------------------------------*/

.macro FAKE_RET_FROM_EXCPN

	lr	r9, [status32]
	bclr	r9, r9, STATUS_AE_BIT
	or	r9, r9, (STATUS_E1_MASK|STATUS_E2_MASK)
	sr	r9, [erstatus]
	mov	r9, 55f
	sr	r9, [eret]
	rtie
55:
.endm

/*--------------------------------------------------------------
 * For early Exception/ISR Prologue, a core reg is temporarily needed to
 * code the rest of prolog (stack switching). This is done by stashing
 * it to memory (non-SMP case) or SCRATCH0 Aux Reg (SMP).
 *
 * Before saving the full regfile - this reg is restored back, only
 * to be saved again on kernel mode stack, as part of pt_regs.
 *-------------------------------------------------------------*/
.macro PROLOG_FREEUP_REG	reg, mem
	st  \reg, [\mem]
.endm

.macro PROLOG_RESTORE_REG	reg, mem
	ld  \reg, [\mem]
.endm

/*--------------------------------------------------------------
 * Exception Entry prologue
 * -Switches stack to K mode (if not already)
 * -Saves the register file
 *
 * After this it is safe to call the "C" handlers
 *-------------------------------------------------------------*/
.macro EXCEPTION_PROLOGUE_KEEP_AE

	/* Need at least 1 reg to code the early exception prologue */
	PROLOG_FREEUP_REG r9, @ex_saved_reg1

	/* U/K mode at time of exception (stack not switched if already K) */
	lr  r9, [erstatus]

	/* ARC700 doesn't provide auto-stack switching */
	SWITCH_TO_KERNEL_STK

	st.a	r0, [sp, -8]    /* orig_r0 needed for syscall (skip ECR slot) */
	sub	sp, sp, 4	/* skip pt_regs->sp, already saved above */

	/* Restore r9 used to code the early prologue */
	PROLOG_RESTORE_REG  r9, @ex_saved_reg1

	/* now we are ready to save the regfile */
	SAVE_R0_TO_R12
	PUSH	gp
	PUSH	fp
	PUSH	blink
	PUSHAX	eret
	PUSHAX	erstatus
	PUSH	lp_count
	PUSHAX	lp_end
	PUSHAX	lp_start
	PUSHAX	erbta

	lr	r10, [ecr]
	st      r10, [sp, PT_event]

#ifdef CONFIG_ARC_CURR_IN_REG
	/* gp already saved on stack: now load with "current" */
	GET_CURR_TASK_ON_CPU   gp
#endif
	; OUTPUT: r10 has ECR expected by EV_Trap
.endm

.macro EXCEPTION_PROLOGUE

	EXCEPTION_PROLOGUE_KEEP_AE	; return ECR in r10

	lr  r0, [efa]
	mov r1, sp

	FAKE_RET_FROM_EXCPN		; clobbers r9
.endm

/*--------------------------------------------------------------
 * Restore all registers used by system call or Exceptions
 * SP should always be pointing to the next free stack element
 * when entering this macro.
 *
 * NOTE:
 *
 * It is recommended that lp_count/ilink1/ilink2 not be used as a dest reg
 * for memory load operations. If used in that way interrupts are deferred
 * by hardware and that is not good.
 *-------------------------------------------------------------*/
.macro EXCEPTION_EPILOGUE

	POPAX	erbta
	POPAX	lp_start
	POPAX	lp_end

	POP	r9
	mov	lp_count, r9	;LD to lp_count is not allowed

	POPAX	erstatus
	POPAX	eret
	POP	blink
	POP	fp
	POP	gp
	RESTORE_R12_TO_R0

	ld  sp, [sp] /* restore original sp */
	/* orig_r0, ECR skipped automatically */
.endm

/* Dummy ECR values for Interrupts */
#define event_IRQ1		0x0031abcd
#define event_IRQ2		0x0032abcd

.macro INTERRUPT_PROLOGUE  LVL

	/* free up r9 as scratchpad */
	PROLOG_FREEUP_REG r9, @int\LVL\()_saved_reg

	/* Which mode (user/kernel) was the system in when intr occurred */
	lr  r9, [status32_l\LVL\()]

	SWITCH_TO_KERNEL_STK


	st.a	0x003\LVL\()abcd, [sp, -4]	/* Dummy ECR */
	sub	sp, sp, 8	    /* skip orig_r0 (not needed)
				       skip pt_regs->sp, already saved above */

	/* Restore r9 used to code the early prologue */
	PROLOG_RESTORE_REG  r9, @int\LVL\()_saved_reg

	SAVE_R0_TO_R12
	PUSH	gp
	PUSH	fp
	PUSH	blink
	PUSH	ilink\LVL\()
	PUSHAX	status32_l\LVL\()
	PUSH	lp_count
	PUSHAX	lp_end
	PUSHAX	lp_start
	PUSHAX	bta_l\LVL\()

#ifdef CONFIG_ARC_CURR_IN_REG
	/* gp already saved on stack: now load with "current" */
	GET_CURR_TASK_ON_CPU   gp
#endif
.endm

/*--------------------------------------------------------------
 * Restore all registers used by interrupt handlers.
 *
 * NOTE:
 *
 * It is recommended that lp_count/ilink1/ilink2 not be used as a dest reg
 * for memory load operations. If used in that way interrupts are deferred
 * by hardware and that is not good.
 *-------------------------------------------------------------*/
.macro INTERRUPT_EPILOGUE  LVL

	POPAX	bta_l\LVL\()
	POPAX	lp_start
	POPAX	lp_end

	POP	r9
	mov	lp_count, r9	;LD to lp_count is not allowed

	POPAX	status32_l\LVL\()
	POP	ilink\LVL\()
	POP	blink
	POP	fp
	POP	gp
	RESTORE_R12_TO_R0

	ld  sp, [sp] /* restore original sp; orig_r0, ECR skipped implicitly */
.endm

/* Get thread_info of "current" tsk */
.macro GET_CURR_THR_INFO_FROM_SP  reg
	bic \reg, sp, (THREAD_SIZE - 1)
.endm

/* Get CPU-ID of this core */
.macro  GET_CPU_ID  reg
	lr  \reg, [identity]
	lsr \reg, \reg, 8
	bmsk \reg, \reg, 7
.endm

#endif  /* __ASM_ARC_ENTRY_COMPACT_H */
