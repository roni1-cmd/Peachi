/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_USER_H
#define _ALPHA_USER_H

#include <linux/sched.h>
#include <linux/ptrace.h>

#include <asm/page.h>
#include <asm/reg.h>

/*
 * Core file format: The core file is written in such a way that gdb
 * can understand it and provide useful information to the user (under
 * linux we use the `trad-core' bfd, NOT the osf-core).  The file contents
 * are as follows:
 *
 *  upage: 1 page consisting of a user struct that tells gdb
 *	what is present in the file.  Directly after this is a
 *	copy of the task_struct, which is currently not used by gdb,
 *	but it may come in handy at some point.  All of the registers
 *	are stored as part of the upage.  The upage should always be
 *	only one page long.
 *  data: The data segment follows next.  We use current->end_text to
 *	current->brk to pick up all of the user variables, plus any memory
 *	that may have been sbrk'ed.  No attempt is made to determine if a
 *	page is demand-zero or if a page is totally unused, we just cover
 *	the entire range.  All of the addresses are rounded in such a way
 *	that an integral number of pages is written.
 *  stack: We need the stack information in order to get a meaningful
 *	backtrace.  We need to write the data from usp to
 *	current->start_stack, so we round each of these in order to be able
 *	to write an integer number of pages.
 */
struct user {
	unsigned long	regs[EF_SIZE/8+32];	/* integer and fp regs */
	size_t		u_tsize;		/* text size (pages) */
	size_t		u_dsize;		/* data size (pages) */
	size_t		u_ssize;		/* stack size (pages) */
	unsigned long	start_code;		/* text starting address */
	unsigned long	start_data;		/* data starting address */
	unsigned long	start_stack;		/* stack starting address */
	long int	signal;			/* signal causing core dump */
	unsigned long	u_ar0;			/* help gdb find registers */
	unsigned long	magic;			/* identifies a core file */
	char		u_comm[32];		/* user command name */
};

#endif /* _ALPHA_USER_H */
