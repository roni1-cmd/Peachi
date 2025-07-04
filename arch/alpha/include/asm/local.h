/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_LOCAL_H
#define _ALPHA_LOCAL_H

#include <linux/percpu.h>
#include <linux/atomic.h>

typedef struct
{
	atomic_long_t a;
} local_t;

#define LOCAL_INIT(i)	{ ATOMIC_LONG_INIT(i) }
#define local_read(l)	atomic_long_read(&(l)->a)
#define local_set(l,i)	atomic_long_set(&(l)->a, (i))
#define local_inc(l)	atomic_long_inc(&(l)->a)
#define local_dec(l)	atomic_long_dec(&(l)->a)
#define local_add(i,l)	atomic_long_add((i),(&(l)->a))
#define local_sub(i,l)	atomic_long_sub((i),(&(l)->a))

static __inline__ long local_add_return(long i, local_t * l)
{
	long temp, result;
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	addq %0,%3,%2\n"
	"	addq %0,%3,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (l->a.counter), "=&r" (result)
	:"Ir" (i), "m" (l->a.counter) : "memory");
	return result;
}

static __inline__ long local_sub_return(long i, local_t * l)
{
	long temp, result;
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	subq %0,%3,%2\n"
	"	subq %0,%3,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (l->a.counter), "=&r" (result)
	:"Ir" (i), "m" (l->a.counter) : "memory");
	return result;
}

static __inline__ long local_cmpxchg(local_t *l, long old, long new)
{
	return cmpxchg_local(&l->a.counter, old, new);
}

static __inline__ bool local_try_cmpxchg(local_t *l, long *old, long new)
{
	return try_cmpxchg_local(&l->a.counter, (s64 *)old, new);
}

#define local_xchg(l, n) (xchg_local(&((l)->a.counter), (n)))

/**
 * local_add_unless - add unless the number is already a given value
 * @l: pointer of type local_t
 * @a: the amount to add to l...
 * @u: ...unless l is equal to u.
 *
 * Atomically adds @a to @l, if @v was not already @u.
 * Returns true if the addition was done.
 */
static __inline__ bool
local_add_unless(local_t *l, long a, long u)
{
	long c = local_read(l);

	do {
		if (unlikely(c == u))
			return false;
	} while (!local_try_cmpxchg(l, &c, c + a));

	return true;
}

#define local_inc_not_zero(l) local_add_unless((l), 1, 0)

#define local_add_negative(a, l) (local_add_return((a), (l)) < 0)

#define local_dec_return(l) local_sub_return(1,(l))

#define local_inc_return(l) local_add_return(1,(l))

#define local_sub_and_test(i,l) (local_sub_return((i), (l)) == 0)

#define local_inc_and_test(l) (local_add_return(1, (l)) == 0)

#define local_dec_and_test(l) (local_sub_return(1, (l)) == 0)

/* Verify if faster than atomic ops */
#define __local_inc(l)		((l)->a.counter++)
#define __local_dec(l)		((l)->a.counter++)
#define __local_add(i,l)	((l)->a.counter+=(i))
#define __local_sub(i,l)	((l)->a.counter-=(i))

#endif /* _ALPHA_LOCAL_H */
