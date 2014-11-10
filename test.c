#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "dqueue.h"

#define NTHREADS 64
#define WORKLOAD 8192

int nthreads;
dqueue_t q;
pthread_t t[NTHREADS];

volatile int start_flag = 0;
volatile int end_count = 0;

// int r[NTHREADS][WORKLOAD];
unsigned long long ts[NTHREADS];

static __inline__ unsigned long long rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

void *
entry(void *_) {
    int index = (unsigned long)_ + 1;
    ts[index - 1] = 0;

    while (!start_flag) asm volatile ("pause");
    unsigned long long tt;
    
    int i;
    for (i = 0; i < WORKLOAD; ++ i) {
        tt = rdtsc();
        assert(dqueue_push(q, (void *)(unsigned long)(index * WORKLOAD + i)));
        ts[index - 1] += rdtsc() - tt;
    }

    void *d;
    for (i = 0; i < WORKLOAD; ++ i) {
        tt = rdtsc();
        assert(dqueue_pop(q, &d));
        // r[index - 1][i] = (int)(unsigned long)d;
        ts[index - 1] += rdtsc() - tt;
    }

    while (1) {
        int old = end_count;
        if (__sync_bool_compare_and_swap(&end_count, old, old + 1))
            break;
    }

    return NULL;
}

int
main(int argc, char **argv) {
    // memset(r, 0, sizeof(r));

    if (argc > 1) nthreads = atoi(argv[1]);
    else nthreads = 4;
    
    q = dqueue_create(nthreads * WORKLOAD * 2);

    int i;
    for (i = 1; i < nthreads; ++ i) 
        pthread_create(&t[i], NULL, entry, (void *)(unsigned long)i);

    usleep(10000);
    start_flag = 1;
    
    entry((void *)0);

    while (end_count < nthreads)
        asm volatile ("pause");

    dqueue_destroy(q);

    unsigned long long sum = 0;
    for (i = 0; i < nthreads; ++ i) {
        // printf("%lld\n", ts[i]);
        sum += ts[i];
    }
    printf("%llu\n", sum);

    /* for (i = 0; i < nthreads; ++ i) { */
    /*     int j; */
    /*     for (j = 0; j < WORKLOAD; ++ j) { */
    /*         printf("%d\n", r[i][j]); */
    /*     } */
    /* } */
    
    return 0;
}
