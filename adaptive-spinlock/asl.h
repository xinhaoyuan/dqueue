#ifndef __ASL_H__
#define __ASL_H__

#define ASL_LOCK_SPINLOCK_RELEASED 0
#define ASL_LOCK_SPINLOCK_ACQUIRED 1
#define ASL_LOCK_MCSLOCK           2

typedef struct asl_s *asl_t;
typedef struct asl_s {
    int   volatile lock;
    asl_t volatile next;
} asl_s;

void acl_acquire(asl_t lock, asl_t local);
void acl_release(asl_t lock, asl_t local);

#endif
