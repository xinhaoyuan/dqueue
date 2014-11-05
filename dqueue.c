#include "dqueue.h"
#include <stdlib.h>
#include <string.h>

dqueue_t
dqueue_create(unsigned int size) {
    dqueue_t q = (dqueue_t)malloc(sizeof(dqueue_s) + sizeof(void *) * size);
    /* assume the address allocated is cache aligned */
    assert((unsigned long)q & (CACHE_LINE_SIZE - 1) == 0);
    if (q) {
        q->req = NULL;
        q->size = size;
        q->head = q->tail = 0;
    }
    return q;
}

void
dqueue_destroy(dqueue_t q) {
    free(q);
}

#define CAS __sync_val_compare_and_swap
#define SWAP(p, v)              /* TODO */

inline void
__dqueue_process_requests_after(dqueue_t q, dqueue_request_t req) {
    dqueue_request_t cur;
    
    if (!(cur = req->next)) {
        if (CAS(&q->req, &req, NULL) == &req)
            return 0;
        else {
            while (!(cur = req->next))
                asm volatile ("pause");
        }
    }
    
    while (req = cur) {
        
        while (req->req_type != OP_UNINITIALIZED)
            asm volatile ("pause");
        
        if (req->req_type == OP_PUSH) 
            if (!__dqueue_push(q, req->data))
                req->data = NULL;
        else if (cur->req_type == OP_POP)
            req->data = __dqueue_pop(q);
        
        req->req_type = OP_FINISHED;
        
        if (!(cur = req->next)) {
            if (CAS(&q->req, &req, NULL) == &req)
                continue;
            else {
                while (!(cur = req->next))
                    asm volatile ("pause");
            }
        }
        __sync_synchonrize();
        req->ready = 1;
    }

}

inline int
__dqueue_push(dqueue_t q, void *data) {
    int t = (q->tail + 1) % q->size;
    if (t == q->head)
        return 0;               /* full */
    q->data[q->tail = t] = data;
    return 1;
}

inline void *
__dqueue_pop(dqueue_t q) {
    if (q->tail == q->head)
        return NULL;               /* empty */
    void *ret = q->data[q->head];
    q->head = (q->head + 1) % q->size;
    return ret;
}

int
dqueue_push(dqueue_t q, void *data) {
    dqueue_request_s req;
    memset(&req, 0, sizeof(req));
    dqueue_request_t cur = SWAP(&q->req, &req);
    if (cur) {
        cur->next = req;
        req.data = data;
        __sync_synchonrize();
        
        req.req_type = OP_PUSH;
        while (!req.ready)
            asm volatile ("pause");

        if (req.req_type == OP_FINISHED)
            return req.data == data;
    }

    int ret = __dqueue_push(q, data);
    __dqueue_process_requests_after(q, req);
    return ret;
}

int
dqueue_pop(dqueue_t q, void **data) {
    dqueue_request_s req;
    memset(&req, 0, sizeof(req));
    dqueue_request_t cur = SWAP(&q->req, &req);
    if (cur) {
        cur->next = req;
        req.data = data;
        __sync_synchonrize();
        
        req.req_type = OP_PUSH;
        while (!req.ready)
            asm volatile ("pause");

        if (req.req_type == OP_FINISHED) {
            if (data) *data = req.data;
            return req.data != NULL;
        }
    }

    void *ret = __dqueue_pop(q);
    if (data) *data = ret;
    __dqueue_process_requests_after(q, req);
    return ret != NULL;
}
