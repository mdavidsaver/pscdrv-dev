/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef SYS_HOST_COMPAT_H
#define SYS_HOST_COMPAT_H

#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;

typedef pthread_mutex_t sys_mutex_t;

static inline int sys_mutex_init(sys_mutex_t *m)
{
    return pthread_mutex_init(m, NULL);
}

#define sys_mutex_free(M) pthread_mutex_destroy(M)

static inline void sys_mutex_lock(sys_mutex_t *m)
{
    int r = pthread_mutex_lock(m);
    assert(r==0);
}

static inline void sys_mutex_unlock(sys_mutex_t *m)
{
    int r = pthread_mutex_unlock(m);
    assert(r==0);
}

typedef pthread_t sys_thread_t;

static inline sys_thread_t sys_thread_new(const char *name,
                                          void (*fn)(void*),
                                          void *arg,
                                          int stack,
                                          int prio)
{
    pthread_t thr;
    int ret = pthread_create(&thr, NULL, fn, arg);
    assert(ret==0);
    if(ret)
        return 0;
    ret = pthread_detach(thr);
    assert(ret==0);
    return thr;
}

static inline void sys_msleep(unsigned int ms)
{
    if(ms>=1000)
        sleep(ms/1000);
    else
        usleep(ms*1000);
}

#endif // SYS_HOST_COMPAT_H
