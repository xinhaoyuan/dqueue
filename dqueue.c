#include "dqueue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

void *aligned_malloc(int size, int align) {
    void *mem = malloc(size+align+sizeof(void*));
    void **ptr = (void**)((long)(mem+align+sizeof(void*)) & ~(align-1));
    ptr[-1] = mem;
    return ptr;
}

void aligned_free(void *ptr) {
    free(((void**)ptr)[-1]);
}

dqueue_t
dqueue_create(unsigned int size) {
    dqueue_t q = (dqueue_t)aligned_malloc(sizeof(dqueue_s) + sizeof(void *) * size, CACHE_LINE_SIZE);
    /* assume the address allocated is cache aligned */
    assert(((unsigned long)q & (CACHE_LINE_SIZE - 1)) == 0);
    if (q) {
        q->req = NULL;
        q->size = size;
        q->head = q->tail = 0;
    }
    return q;
}

void
dqueue_destroy(dqueue_t q) {
    aligned_free(q);
}

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

inline int
__dqueue_push(dqueue_t q, void *data) {
    int t = (q->tail + 1) % q->size;
    if (t == q->head)
        return 0;               /* full */
    q->entry[q->tail] = data;
    q->tail = t;
    return 1;
}

inline void *
__dqueue_pop(dqueue_t q) {
    if (q->tail == q->head) {
        return NULL;               /* empty */
    }
    void *ret = q->entry[q->head];
    q->head = (q->head + 1) % q->size;
    return ret;
}

#ifndef HELP_THRESHOLD
#define HELP_THRESHOLD 10
#endif

inline void
__dqueue_process_requests_after(dqueue_t q, dqueue_request_t req) {
    dqueue_request_t cur;
    
    if (!(cur = req->next)) {
        if (CAS(&q->req, req, NULL) == req)
            return;
        else {
            // printf("<\n");
            while (!(cur = req->next))
                asm volatile ("pause");
            // printf(">\n");
        }
    }

    int help_count = 0;

    while ((req = cur) &&
           (++ help_count <= HELP_THRESHOLD)) {
        unsigned int req_type;
        // printf("<\n");
        while ((req_type = req->req_type) == OP_UNINITIALIZED)
            asm volatile ("pause");
        // printf(">\n");
        /* fulfill the next request */
        if (req_type == OP_PUSH) {
            if (!__dqueue_push(q, req->data))
                req->data = NULL;
        } else if (req_type == OP_POP) 
            req->data = __dqueue_pop(q);
        __sync_synchronize();
        req->req_type = OP_FINISHED;
        /* fetch the next pointer */
        if (!(cur = req->next)) {
            if (CAS(&q->req, req, NULL) == req) {
                break;
            } else {
                // printf("< %p %p %p\n", q->req, req, req->next);
                while (!(cur = req->next))
                    asm volatile ("pause");
                // printf(">\n");
            }
        }
        /* the the request to be ready */
        req->ready = 1;
    }

    if (req)
        req->ready = 1;
}

int
dqueue_push(dqueue_t q, void *data) {
    if (data == NULL) return 0;
#if LOCK == 0
    while (CAS(&q->req, NULL, (void *)1) == (void *)1)
        asm volatile ("pause");
#elif LOCK == 1
    dqueue_request_s req;
    memset(&req, 0, sizeof(req));
    dqueue_request_t cur = SWAP(&q->req, &req);
    if (cur) {
        // printf("set %p->next\n", cur);
        cur->next = &req;
        req.data = data;
        __sync_synchronize();
        
        req.req_type = OP_PUSH;
        while (!req.ready)
            asm volatile ("pause");

        if (req.req_type == OP_FINISHED) {
            return req.data == data;
        }
    }
#endif

    int ret = __dqueue_push(q, data);
#if LOCK == 0
    q->req = NULL;
#elif LOCK == 1
    __dqueue_process_requests_after(q, &req);
#endif
    return ret;
}

int
dqueue_pop(dqueue_t q, void **data) {
#if LOCK == 0
    while (CAS(&q->req, NULL, (void *)1) == (void *)1)
        asm volatile ("pause");
#elif LOCK == 1
    dqueue_request_s req;
    memset(&req, 0, sizeof(req));
    dqueue_request_t cur = SWAP(&q->req, &req);
    if (cur) {
        cur->next = &req;
        req.req_type = OP_POP;

        while (!req.ready)
            asm volatile ("pause");
        
        if (req.req_type == OP_FINISHED) {
            if (data) *data = req.data;
            return req.data != NULL;
        }
    }
#endif
    
    void *ret = __dqueue_pop(q);
    if (data) *data = ret;
#if LOCK == 0
    q->req = NULL;
#elif LOCK == 1
    __dqueue_process_requests_after(q, &req);
#endif
    return ret != NULL;
}
