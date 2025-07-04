/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/clear_user.S
 * Contributed by Richard Henderson <rth@tamu.edu>
 *
 * Zero user space, handling exceptions as we go.
 *
 * We have to make sure that $0 is always up-to-date and contains the
 * right "bytes left to zero" value (and that it is updated only _after_
 * a successful copy).  There is also some rather minor exception setup
 * stuff.
 */
#include <linux/export.h>

/* Allow an exception for an insn; exit if we get one.  */
#define EX(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda $31, $exception-99b($31); 	\
	.previous

	.set noat
	.set noreorder
	.align 4

	.globl __clear_user
	.ent __clear_user
	.frame	$30, 0, $26
	.prologue 0

$loop:
	and	$1, 3, $4	# e0    :
	beq	$4, 1f		# .. e1 :

0:	EX( stq_u $31, 0($16) )	# e0    : zero one word
	subq	$0, 8, $0	# .. e1 :
	subq	$4, 1, $4	# e0    :
	addq	$16, 8, $16	# .. e1 :
	bne	$4, 0b		# e1    :
	unop			#       :

1:	bic	$1, 3, $1	# e0    :
	beq	$1, $tail	# .. e1 :

2:	EX( stq_u $31, 0($16) )	# e0    : zero four words
	subq	$0, 8, $0	# .. e1 :
	EX( stq_u $31, 8($16) )	# e0    :
	subq	$0, 8, $0	# .. e1 :
	EX( stq_u $31, 16($16) )	# e0    :
	subq	$0, 8, $0	# .. e1 :
	EX( stq_u $31, 24($16) )	# e0    :
	subq	$0, 8, $0	# .. e1 :
	subq	$1, 4, $1	# e0    :
	addq	$16, 32, $16	# .. e1 :
	bne	$1, 2b		# e1    :

$tail:
	bne	$2, 1f		# e1    : is there a tail to do?
	ret	$31, ($26), 1	# .. e1 :

1:	EX( ldq_u $5, 0($16) )	# e0    :
	clr	$0		# .. e1 :
	nop			# e1    :
	mskqh	$5, $0, $5	# e0    :
	EX( stq_u $5, 0($16) )	# e0    :
	ret	$31, ($26), 1	# .. e1 :

__clear_user:
	and	$17, $17, $0
	and	$16, 7, $4	# e0    : find dest misalignment
	beq	$0, $zerolength # .. e1 :
	addq	$0, $4, $1	# e0    : bias counter
	and	$1, 7, $2	# e1    : number of bytes in tail
	srl	$1, 3, $1	# e0    :
	beq	$4, $loop	# .. e1 :

	EX( ldq_u $5, 0($16) )	# e0    : load dst word to mask back in
	beq	$1, $oneword	# .. e1 : sub-word store?

	mskql	$5, $16, $5	# e0    : take care of misaligned head
	addq	$16, 8, $16	# .. e1 :
	EX( stq_u $5, -8($16) )	# e0    :
	addq	$0, $4, $0	# .. e1 : bytes left -= 8 - misalignment
	subq	$1, 1, $1	# e0    :
	subq	$0, 8, $0	# .. e1 :
	br	$loop		# e1    :
	unop			#       :

$oneword:
	mskql	$5, $16, $4	# e0    :
	mskqh	$5, $2, $5	# e0    :
	or	$5, $4, $5	# e1    :
	EX( stq_u $5, 0($16) )	# e0    :
	clr	$0		# .. e1 :

$zerolength:
$exception:
	ret	$31, ($26), 1	# .. e1 :

	.end __clear_user
	EXPORT_SYMBOL(__clear_user)
