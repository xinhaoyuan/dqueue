#include "asl.h"
#include <assert.h>
#include <stdio.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define CAS __sync_val_compare_and_swap

/*
 * Constants for operation sizes. On 32-bit, the 64-bit size it set to
 * -1 because sizeof will never return -1, thereby making those switch
 * case statements guaranteeed dead code which the compiler will
 * eliminate, and allowing the "missing symbol in the default case" to
 * indicate a usage error.
 */
#define __X86_CASE_B	1
#define __X86_CASE_W	2
#define __X86_CASE_L	4
#define __X86_CASE_Q	8

/* 
 * An exchange-type operation, which takes a value and a pointer, and
 * returns the old value.
 */
#define __xchg_op(ptr, arg, op, lock)					\
	({                                                  \
        __typeof__ (*(ptr)) __ret = (arg);              \
		switch (sizeof(*(ptr))) {                       \
		case __X86_CASE_B:                              \
			asm volatile (lock #op "b %b0, %1\n"		\
                          : "+q" (__ret), "+m" (*(ptr))	\
                          : : "memory", "cc");          \
			break;                                      \
		case __X86_CASE_W:                              \
			asm volatile (lock #op "w %w0, %1\n"		\
                          : "+r" (__ret), "+m" (*(ptr))	\
                          : : "memory", "cc");          \
			break;                                      \
		case __X86_CASE_L:                              \
			asm volatile (lock #op "l %0, %1\n"         \
                          : "+r" (__ret), "+m" (*(ptr))	\
                          : : "memory", "cc");          \
			break;                                      \
		case __X86_CASE_Q:                              \
			asm volatile (lock #op "q %q0, %1\n"		\
                          : "+r" (__ret), "+m" (*(ptr))	\
                          : : "memory", "cc");          \
			break;                                      \
		}                                               \
		__ret;                                          \
	})

/*
 * Note: no "lock" prefix even on SMP: xchg always implies lock anyway.
 * Since this is generally used to protect other memory information, we
 * use "asm volatile" and "memory" clobbers to prevent gcc from moving
 * information around.
 */
#define xchg(ptr, v)	__xchg_op((ptr), (v), xchg, "")

#define SWAP(p, v) xchg(p, v)

void
asl_acquire(asl_t lock, asl_t local) {
    int type;
    int try = 0;
    while (++ try <= 3) {
        type = CAS(&lock->lock,
                   ASL_LOCK_SPINLOCK_RELEASED,
                   ASL_LOCK_SPINLOCK_ACQUIRED);
        if (type == ASL_LOCK_SPINLOCK_RELEASED) {
            local->lock = ASL_LOCK_SPINLOCK_ACQUIRED;
            return;
        } else if (type != ASL_LOCK_SPINLOCK_ACQUIRED)
            break;
    }

    /* MCS lock here */
    local->lock = ASL_LOCK_MCSLOCK;
    local->next = NULL;
    asl_t cur = SWAP(&lock->next, local);
    if (cur) {
        cur->next = local;
        while (local->lock == ASL_LOCK_MCSLOCK)
            asm volatile ("pause");
    } else {
        /* ensure it is really MCS lock */
        while (CAS(&lock->lock,
                   ASL_LOCK_SPINLOCK_RELEASED,
                   ASL_LOCK_MCSLOCK) ==
               ASL_LOCK_SPINLOCK_ACQUIRED)
            asm volatile ("pause");
    }
}

void
asl_release(asl_t lock, asl_t local) {
    if (local->lock == ASL_LOCK_SPINLOCK_ACQUIRED) {
        lock->lock = ASL_LOCK_SPINLOCK_RELEASED;
    } else {
        asl_t cur;
        if (!(cur = local->next)) {
            if (CAS(&lock->next, local, NULL) == local) {
                lock->lock = ASL_LOCK_SPINLOCK_RELEASED;
                return;
            } else {
                while (!(cur = local->next))
                    asm volatile ("pause");
            }
        }
        cur->lock = ASL_LOCK_SPINLOCK_RELEASED;
    }
}
