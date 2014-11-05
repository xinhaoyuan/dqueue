#ifndef __DQUEUE_H__
#define __DQUEUE_H__

#define CACHE_LINE_SIZE 64

typedef struct dqueue_request_s *dqueue_request_t;

#define OP_UNINITIALIZED 0
#define OP_PUSH          1
#define OP_POP           2
#define OP_FINISHED      3

typedef struct dqueue_request_s {
    volatile unsigned int req_type;
    void * volatile data;
    volatile dqueue_request_t next;
    volatile int ready;
} dqueue_request_s;
    
typedef struct dqueue_s *dqueue_t;
typedef struct dqueue_s {
    volatile dqueue_request_t req __attribute__((aligned(CACHE_LINE_SIZE)));
    unsigned int size, head, tail;
    void *entry[0];             /* place holder */
} dqueue_s;

dqueue_t dqueue_create(unsigned int size);
void     dqueue_destroy(dqueue_t q);

int dqueue_push(dqueue_t q, void *data);
int dqueue_pop(dqueue_t q, void **data);

#endif
