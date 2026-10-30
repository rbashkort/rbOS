/* futextest: raw futex(2) ABI coverage for the P4.1 command set.
 *
 * Exercises the Linux x86-64 futex ABI directly (syscall 202), bypassing
 * mlibc's FutexWait/FutexWake wrappers, so the kernel-side semantics are
 * checked exactly:
 *   1. FUTEX_WAIT woken by FUTEX_WAKE
 *   2. FUTEX_WAKE on an empty word returns 0
 *   3. FUTEX_WAIT_BITSET / FUTEX_WAKE_BITSET masks (non-intersecting mask
 *      must NOT wake; intersecting mask must wake)
 *   4. bitset == 0 -> EINVAL (both BITSET ops)
 *   5. FUTEX_WAIT_BITSET absolute CLOCK_MONOTONIC timeout -> ETIMEDOUT
 *   6. FUTEX_WAIT relative timeout -> ETIMEDOUT
 *   7. FUTEX_REQUEUE moves the surplus waiter to uaddr2; a later wake on
 *      uaddr2 releases it
 *   8. FUTEX_CMP_REQUEUE checks the expected value (EAGAIN on mismatch)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define RB_SYS_FUTEX 202L

#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_REQUEUE     3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE     128

static int fails = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); fails++; } \
} while (0)

/* Raw futex syscall: (uaddr, op, val, arg4, uaddr2, val3).
 * `arg4` is the timeout pointer for WAIT/WAIT_BITSET and the requeue count
 * for REQUEUE/CMP_REQUEUE (hence a raw unsigned long). */
static long futex_sys(uint32_t *uaddr, int op, uint32_t val, unsigned long arg4,
                      uint32_t *uaddr2, uint32_t val3) {
    long ret;
    register long r10 __asm__("r10") = (long)arg4;
    register long r8  __asm__("r8")  = (long)uaddr2;
    register long r9  __asm__("r9")  = (long)val3;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(RB_SYS_FUTEX), "D"((long)uaddr), "S"((long)op), "d"((long)val),
          "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

static volatile int g_armed = 0;
static volatile int g_done = 0;

struct waiter {
    uint32_t *addr;
    uint32_t *addr2;
    int op;
    uint32_t val;
    uint32_t val3;
    unsigned long arg4;
    long ret;
};

static void *waiter_fn(void *p) {
    struct waiter *w = (struct waiter *)p;
    __sync_fetch_and_add(&g_armed, 1);
    w->ret = futex_sys(w->addr, w->op, w->val, w->arg4, w->addr2, w->val3);
    __sync_fetch_and_add(&g_done, 1);
    return NULL;
}

static void rel_ts(struct timespec *ts, long ms) {
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000L;
}

static void abs_mono_ts(struct timespec *ts, long ms) {
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_nsec -= 1000000000L;
        ts->tv_sec++;
    }
}

/* Spin until `n` waiters have signalled they are about to block, then give
 * them time to actually reach the futex queue. */
static void wait_armed(int n) {
    for (int i = 0; i < 2000 && g_armed < n; i++)
        usleep(1000);
    usleep(100000);
}

int main(void) {
    /* 1. plain WAIT woken by WAKE */
    {
        static uint32_t f;
        f = 0; g_armed = 0; g_done = 0;
        struct timespec ts; rel_ts(&ts, 2000);
        struct waiter w = { &f, NULL, FUTEX_PRIVATE | FUTEX_WAIT, 0, 0, (unsigned long)&ts, 0 };
        pthread_t th;
        pthread_create(&th, NULL, waiter_fn, &w);
        wait_armed(1);
        long n = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAKE, 1, 0, NULL, 0);
        CHECK(n == 1, "FUTEX_WAKE wakes one waiter");
        pthread_join(th, NULL);
        CHECK(w.ret == 0, "FUTEX_WAIT returns 0 when woken");
    }

    /* 2. WAKE on an empty futex */
    {
        static uint32_t f;
        f = 0;
        long n = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAKE, 1, 0, NULL, 0);
        CHECK(n == 0, "FUTEX_WAKE on empty futex returns 0");
    }

    /* 3. bitset masking */
    {
        static uint32_t f;
        f = 0; g_armed = 0; g_done = 0;
        struct timespec ts; abs_mono_ts(&ts, 2000);
        struct waiter w = { &f, NULL, FUTEX_PRIVATE | FUTEX_WAIT_BITSET, 0, 0x2,
                            (unsigned long)&ts, 0 };
        pthread_t th;
        pthread_create(&th, NULL, waiter_fn, &w);
        wait_armed(1);
        long n1 = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAKE_BITSET, 1, 0, NULL, 0x1);
        CHECK(n1 == 0, "WAKE_BITSET non-intersecting mask wakes nobody");
        long n2 = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAKE_BITSET, 1, 0, NULL, 0x2);
        CHECK(n2 == 1, "WAKE_BITSET intersecting mask wakes the waiter");
        pthread_join(th, NULL);
        CHECK(w.ret == 0, "FUTEX_WAIT_BITSET returns 0 when woken");
    }

    /* 4. bitset == 0 -> EINVAL */
    {
        static uint32_t f;
        f = 0;
        long a = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAIT_BITSET, 0, 0, NULL, 0);
        CHECK(a == -EINVAL, "WAIT_BITSET with zero bitset is EINVAL");
        long b = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAKE_BITSET, 1, 0, NULL, 0);
        CHECK(b == -EINVAL, "WAKE_BITSET with zero mask is EINVAL");
    }

    /* 5. FUTEX_WAIT_BITSET absolute timeout */
    {
        static uint32_t f;
        f = 0;
        struct timespec ts; abs_mono_ts(&ts, 60);
        long r = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAIT_BITSET, 0,
                           (unsigned long)&ts, NULL, 0xffffffff);
        CHECK(r == -ETIMEDOUT, "WAIT_BITSET absolute timeout is ETIMEDOUT");
    }

    /* 6. FUTEX_WAIT relative timeout */
    {
        static uint32_t f;
        f = 0;
        struct timespec ts; rel_ts(&ts, 60);
        long r = futex_sys(&f, FUTEX_PRIVATE | FUTEX_WAIT, 0,
                           (unsigned long)&ts, NULL, 0);
        CHECK(r == -ETIMEDOUT, "FUTEX_WAIT relative timeout is ETIMEDOUT");
    }

    /* 7. FUTEX_REQUEUE */
    {
        static uint32_t a, b;
        a = 0; b = 0; g_armed = 0; g_done = 0;
        struct timespec ts1, ts2; rel_ts(&ts1, 2000); rel_ts(&ts2, 2000);
        struct waiter w1 = { &a, NULL, FUTEX_PRIVATE | FUTEX_WAIT, 0, 0, (unsigned long)&ts1, 0 };
        struct waiter w2 = { &a, NULL, FUTEX_PRIVATE | FUTEX_WAIT, 0, 0, (unsigned long)&ts2, 0 };
        pthread_t t1, t2;
        pthread_create(&t1, NULL, waiter_fn, &w1);
        pthread_create(&t2, NULL, waiter_fn, &w2);
        wait_armed(2);

        /* wake 1, requeue 1: arg4 carries nr_requeue */
        long n = futex_sys(&a, FUTEX_PRIVATE | FUTEX_REQUEUE, 1, 1, &b, 0);
        CHECK(n == 2, "FUTEX_REQUEUE returns woken+requeued (2)");
        usleep(150000);
        CHECK(g_done == 1, "exactly one waiter woken, one requeued");

        long m = futex_sys(&b, FUTEX_PRIVATE | FUTEX_WAKE, 1, 0, NULL, 0);
        CHECK(m == 1, "WAKE on the requeue target wakes the moved waiter");
        pthread_join(t1, NULL);
        pthread_join(t2, NULL);
        usleep(50000);
        CHECK(g_done == 2, "both waiters finished");
        CHECK(w1.ret == 0 && w2.ret == 0, "requeued waiter returns 0 when woken");
    }

    /* 8. FUTEX_CMP_REQUEUE value check */
    {
        static uint32_t a, b;
        a = 7; b = 0;
        long bad = futex_sys(&a, FUTEX_PRIVATE | FUTEX_CMP_REQUEUE, 0, 0, &b, 9);
        CHECK(bad == -EAGAIN, "CMP_REQUEUE wrong expected value is EAGAIN");
        long ok = futex_sys(&a, FUTEX_PRIVATE | FUTEX_CMP_REQUEUE, 0, 0, &b, 7);
        CHECK(ok == 0, "CMP_REQUEUE matching value with no waiters returns 0");
    }

    /* 9. hash-bucket collision must not cross-wake. `arr[0]` and `arr[64]` are
     *    256 bytes apart, so (addr>>2) differs by 64 -> same 64-bucket wait
     *    queue, yet a WAKE on one must not touch a waiter on the other. */
    {
        static uint32_t arr[128];
        uint32_t *a = &arr[0];
        uint32_t *b = &arr[64];
        *a = 0; *b = 0; g_armed = 0; g_done = 0;
        struct timespec ts; rel_ts(&ts, 2000);
        struct waiter w = { a, NULL, FUTEX_PRIVATE | FUTEX_WAIT, 0, 0, (unsigned long)&ts, 0 };
        pthread_t th;
        pthread_create(&th, NULL, waiter_fn, &w);
        wait_armed(1);
        long nb = futex_sys(b, FUTEX_PRIVATE | FUTEX_WAKE, 1, 0, NULL, 0);
        CHECK(nb == 0, "wake on a colliding address wakes nobody");
        usleep(50000);
        CHECK(g_done == 0, "waiter on a colliding address stays blocked");
        long na = futex_sys(a, FUTEX_PRIVATE | FUTEX_WAKE, 1, 0, NULL, 0);
        CHECK(na == 1, "wake on the true address wakes the waiter");
        pthread_join(th, NULL);
        CHECK(w.ret == 0, "collision-test waiter returns 0 when woken");
    }

    printf(fails ? "futextest: %d FAILURES\n" : "FUTEX-OK\n", fails);
    return fails ? 1 : 0;
}
