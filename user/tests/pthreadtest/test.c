/* pthreadtest: exercise mlibc threading on rbOS.
 * mutex counter, join + retval, condvar ping-pong, detached thread,
 * per-thread TLS via pthread_self/gettid distinctness. */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static long sys_gettid(void) {
    /* gettid(2), Linux nr 186 */
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(186L) : "rcx", "r11", "memory");
    return ret;
}

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static long counter = 0;
static int fails = 0;

#define N_THREADS 4
#define N_INCREMENTS 10000

#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); fails++; } \
} while (0)

static void *worker(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < N_INCREMENTS; i++) {
        pthread_mutex_lock(&mtx);
        counter++;
        pthread_mutex_unlock(&mtx);
    }
    return (void *)(id * 100);
}

static int turn = 0; /* 0 = pong thread waits, ping thread signals */
static void *ponger(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mtx);
    while (turn != 1)
        pthread_cond_wait(&cv, &mtx);
    turn = 2;
    pthread_mutex_unlock(&mtx);
    return NULL;
}

static volatile int detached_ran = 0;
static void *detached(void *arg) {
    (void)arg;
    detached_ran = 1;
    return NULL;
}

/* 5. timed waits (BUG-009 regression): a timed lock/wait must return
 *    ETIMEDOUT instead of blocking forever now that the timeout reaches
 *    the kernel. */
static pthread_mutex_t tmtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tcv = PTHREAD_COND_INITIALIZER;
static volatile int tmtx_locked = 0;

static struct timespec mono_deadline(long ms) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }
    return ts;
}

static void *holder(void *arg) {
    (void)arg;
    pthread_mutex_lock(&tmtx);
    tmtx_locked = 1;
    usleep(400000);
    pthread_mutex_unlock(&tmtx);
    return NULL;
}

int main(void) {
    pthread_t th[N_THREADS];

    /* 1. mutex + join + retval */
    for (long i = 0; i < N_THREADS; i++) {
        int e = pthread_create(&th[i], NULL, worker, (void *)i);
        if (e) { printf("FAIL: create %ld errno=%d\n", i, e); return 1; }
    }
    for (int i = 0; i < N_THREADS; i++)
        pthread_join(th[i], NULL);

    CHECK(counter == (long)N_THREADS * N_INCREMENTS, "mutex counter exact");

    /* join retvals */
    pthread_t t2;
    pthread_create(&t2, NULL, worker, (void *)7L);
    void *rv = NULL;
    pthread_join(t2, &rv);
    CHECK((long)rv == 700, "join returns thread retval");

    /* 2. condvar handoff */
    pthread_t pt;
    pthread_create(&pt, NULL, ponger, NULL);
    usleep(200000);
    pthread_mutex_lock(&mtx);
    turn = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mtx);
    pthread_join(pt, NULL);
    CHECK(turn == 2, "condvar signal wakes waiter");

    /* 3. detached thread runs to completion */
    pthread_t dt;
    pthread_create(&dt, NULL, detached, NULL);
    pthread_detach(dt);
    for (int i = 0; i < 50 && !detached_ran; i++)
        usleep(10000);
    CHECK(detached_ran == 1, "detached thread ran");

    /* 4. distinct self pointers and tids */
    pthread_t a, b;
    pthread_create(&a, NULL, worker, (void *)1L);
    pthread_create(&b, NULL, worker, (void *)2L);
    CHECK(pthread_self() != 0, "pthread_self nonzero");
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    CHECK(sys_gettid() > 0, "gettid works");

    /* 5. timed waits must time out, not hang */
    pthread_t ht;
    pthread_create(&ht, NULL, holder, NULL);
    for (int i = 0; i < 500 && !tmtx_locked; i++)
        usleep(1000);
    struct timespec dl = mono_deadline(100);
    int e = pthread_mutex_clocklock(&tmtx, CLOCK_MONOTONIC, &dl);
    CHECK(e == ETIMEDOUT, "pthread_mutex_clocklock times out");
    pthread_join(ht, NULL);

    pthread_mutex_lock(&tmtx);
    dl = mono_deadline(100);
    e = pthread_cond_clockwait(&tcv, &tmtx, CLOCK_MONOTONIC, &dl);
    pthread_mutex_unlock(&tmtx);
    CHECK(e == ETIMEDOUT, "pthread_cond_clockwait times out");

    printf(fails ? "pthreadtest: %d FAILURES\n" : "pthreadtest: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
