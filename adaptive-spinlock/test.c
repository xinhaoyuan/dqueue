#include "asl.h"
#include <pthread.h>
#include <unistd.h>

#define THREAD_COUNT_MAX 32
#define WORKLOADS 8192

volatile int start_flag = 0;
volatile int end_flag = 0;

asl_s lock = {0};
pthread_t p[THREAD_COUNT_MAX];

void *
entry(void *_) {
    while (start_flag == 0)
        asm volatile ("pause");

    int i;
    asl_s local;
    for (i = 0; i < WORKLOADS; ++ i) {
        asl_acquire(&lock, &local);
        asl_release(&lock, &local);
    }

    while (1) {
        int old = end_flag;
        if (__sync_val_compare_and_swap(&end_flag, old, old + 1) == old)
            break;
        asm volatile ("pause");
    }
    
    return NULL;
}

int
main(int argc, char **argv) {
    int threads = 4;
    if (argc > 1)
        threads = atoi(argv[1]);

    int i;
    for (i = 1; i < threads; ++ i)
        pthread_create(&p[i], NULL, entry, (void *)(long)i);

    usleep(10000);
    start_flag = 1;
    entry((void *)0);

    while (end_flag < threads)
        asm volatile ("pause");

    return 0;
}
