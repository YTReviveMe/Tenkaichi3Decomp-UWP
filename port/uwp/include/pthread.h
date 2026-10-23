#ifndef BT3_UWP_PTHREAD_H
#define BT3_UWP_PTHREAD_H

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#undef near
#undef far
#undef small

typedef HANDLE pthread_t;
typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef INIT_ONCE pthread_once_t;
typedef int pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
#define PTHREAD_CREATE_DETACHED 1

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) { AcquireSRWLockExclusive(mutex); return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) { ReleaseSRWLockExclusive(mutex); return 0; }
static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    return SleepConditionVariableSRW(cond, mutex, INFINITE, 0) ? 0 : -1;
}
static inline int pthread_cond_signal(pthread_cond_t *cond) { WakeConditionVariable(cond); return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *cond) { WakeAllConditionVariable(cond); return 0; }

static BOOL CALLBACK bt3_once(INIT_ONCE *once, void *param, void **context) {
    (void)once; (void)context;
    ((void (*)(void))(uintptr_t)param)();
    return TRUE;
}
static inline int pthread_once(pthread_once_t *once, void (*fn)(void)) {
    return InitOnceExecuteOnce(once, bt3_once, (void *)(uintptr_t)fn, NULL) ? 0 : -1;
}

typedef struct bt3_thread_start { void *(*fn)(void *); void *arg; } bt3_thread_start;
static DWORD WINAPI bt3_thread_entry(void *context) {
    bt3_thread_start *start = (bt3_thread_start *)context;
    void *(*fn)(void *) = start->fn;
    void *arg = start->arg;
    free(start);
    fn(arg);
    return 0;
}
static inline int pthread_attr_init(pthread_attr_t *attr) { *attr = 0; return 0; }
static inline int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) { *attr = state; return 0; }
static inline int pthread_attr_destroy(pthread_attr_t *attr) { (void)attr; return 0; }
static inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*fn)(void *), void *arg) {
    bt3_thread_start *start = (bt3_thread_start *)malloc(sizeof(*start));
    if (!start) return -1;
    start->fn = fn;
    start->arg = arg;
    *thread = CreateThread(NULL, 0, bt3_thread_entry, start, 0, NULL);
    if (!*thread) { free(start); return -1; }
    if (attr && *attr == PTHREAD_CREATE_DETACHED) { CloseHandle(*thread); *thread = NULL; }
    return 0;
}
static inline int pthread_join(pthread_t thread, void **value) {
    (void)value;
    DWORD status = WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return status == WAIT_OBJECT_0 ? 0 : -1;
}
static inline int pthread_detach(pthread_t thread) {
    return CloseHandle(thread) ? 0 : -1;
}

#endif
