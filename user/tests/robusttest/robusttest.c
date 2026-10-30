/* robusttest: raw robust-futex-list ABI for P4.2.
 *
 * mlibc 7.0.0 does not implement robust mutexes (thread_mutex_init asserts
 * PTHREAD_MUTEX_STALLED), so this test drives the kernel ABI directly with
 * syscalls 273/274/202, the way glibc would:
 *
 *   set_robust_list(head, 24) registers a per-thread circular list of
 *   `struct robust_list` entries. At thread exit the kernel walks it, sets
 *   FUTEX_OWNER_DIED on every word this thread still owns (keeping
 *   FUTEX_WAITERS) and wakes one waiter when FUTEX_WAITERS was set.
 *
 * Checked:
 *   1. lock in the list: owner-died + waiter woken
 *   2. lock only in list_op_pending (half-acquired): owner-died + wake
 *   3. lock both listed and pending: processed once, still woken once
 *   4. a listed word owned by another tid is left untouched and not woken
 *   5. self-referential list terminates (ROBUST_LIST_LIMIT)
 *   6. set_robust_list wrong length -> EINVAL
 *   7. get_robust_list returns the head and 24
 *   8. get_robust_list unknown tid -> ESRCH
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define RB_SYS_FUTEX           202L
#define RB_SYS_GETTID          186L
#define RB_SYS_SET_ROBUST_LIST 273L
#define RB_SYS_GET_ROBUST_LIST 274L

#define FUTEX_WAIT    0
#define FUTEX_WAKE    1
#define FUTEX_PRIVATE 128

#define FUTEX_WAITERS    0x80000000u
#define FUTEX_OWNER_DIED 0x40000000u
#define FUTEX_TID_MASK   0x3fffffffu

struct robust_list {
    struct robust_list *next;
};

struct robust_list_head {
    struct robust_list list;
    long futex_offset;
    struct robust_list *list_op_pending;
};

static int fails = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); fails++; } \
} while (0)

static long sys2(long nr, unsigned long a1, unsigned long a2) {
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static long sys3(long nr, unsigned long a1, unsigned long a2, unsigned long a3) {
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static long raw_gettid(void) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(RB_SYS_GETTID)
        : "rcx", "r11", "memory");
    return ret;
}

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

/* A mutex-like object: the futex word at offset 0, the robust list entry at
 * offset 8, so futex_offset = -8 (uaddr = entry + futex_offset). */
struct rmutex {
    uint32_t futex;
    uint32_t pad;
    struct robust_list list;
};

static volatile int g_armed = 0;
static volatile int g_done = 0;

struct waiter {
    uint32_t *addr;
    int op;
    uint32_t val;
    unsigned long arg4;
    long ret;
};

static void *waiter_fn(void *p) {
    struct waiter *w = (struct waiter *)p;
    __sync_fetch_and_add(&g_armed, 1);
    w->ret = futex_sys(w->addr, w->op, w->val, w->arg4, NULL, 0);
    __sync_fetch_and_add(&g_done, 1);
    return NULL;
}

static void wait_armed(int n) {
    for (int i = 0; i < 3000 && g_armed < n; i++)
        usleep(1000);
    usleep(100000);
}

/* Owner modes. */
enum {
    M_LIST = 0,     /* lock in the list */
    M_PENDING = 1,  /* lock only in list_op_pending */
    M_BOTH = 2,     /* lock both listed and pending */
    M_LOOP = 3,     /* self-referential list (safety) */
    M_TWO = 4,      /* owned lock + another tid's lock in one list */
};

struct owner_arg {
    struct robust_list_head *head;
    struct rmutex *m;
    struct rmutex *m2;      /* M_TWO: word owned by someone else */
    uint32_t other_tid;
    int mode;
    volatile int ready;
    volatile int go;
    long owner_tid;
    long set_ret;
};

static void *owner_fn(void *p) {
    struct owner_arg *a = (struct owner_arg *)p;
    long tid = raw_gettid();
    a->owner_tid = tid;
    a->set_ret = 0;

    a->head->futex_offset = (char *)&a->m->futex - (char *)&a->m->list;
    a->head->list_op_pending = NULL;
    a->head->list.next = &a->head->list; /* empty */

    /* Take the lock(s). */
    a->m->futex = (uint32_t)tid | FUTEX_WAITERS;
    a->m->list.next = &a->head->list;

    switch (a->mode) {
    case M_LIST:
        a->head->list.next = &a->m->list;
        break;
    case M_PENDING:
        a->head->list_op_pending = &a->m->list;
        break;
    case M_BOTH:
        a->head->list.next = &a->m->list;
        a->head->list_op_pending = &a->m->list;
        break;
    case M_LOOP:
        a->head->list.next = &a->m->list;
        a->m->list.next = &a->m->list;   /* infinite cycle */
        break;
    case M_TWO:
        a->m2->futex = a->other_tid | FUTEX_WAITERS;
        a->m2->list.next = &a->head->list;
        a->m->list.next = &a->m2->list;
        a->head->list.next = &a->m->list;
        break;
    }

    a->set_ret = sys2(RB_SYS_SET_ROBUST_LIST,
                      (unsigned long)a->head, sizeof(struct robust_list_head));
    a->ready = 1;
    while (!a->go)
        usleep(1000);
    return NULL; /* thread exit -> kernel walks the robust list */
}

static void rel_ts(struct timespec *ts, long ms) {
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000L;
}

struct robust_list_head g_head;
struct rmutex g_m, g_m2;

/* Start an owner and wait until its lock word is live and registered. */
static pthread_t start_owner(struct owner_arg *oa, int mode, struct rmutex *m2,
                             uint32_t other_tid) {
    oa->head = &g_head;
    oa->m = &g_m;
    oa->m2 = m2;
    oa->other_tid = other_tid;
    oa->mode = mode;
    oa->ready = 0;
    oa->go = 0;
    g_m.futex = 0;
    g_m.list.next = NULL;
    if (m2) {
        m2->futex = 0;
        m2->list.next = NULL;
    }
    pthread_t th;
    pthread_create(&th, NULL, owner_fn, oa);
    for (int i = 0; i < 3000 && !oa->ready; i++)
        usleep(1000);
    return th;
}

/* Run one waiter against `m`, let the owner exit, and check the result. */
static void run_case(int mode, const char *wake_name, const char *word_name) {
    struct owner_arg oa = { 0 };
    struct timespec ts;
    struct waiter w;
    pthread_t owner, waiter;
    long expected;
    int rc;

    rel_ts(&ts, 5000);
    g_armed = 0;
    g_done = 0;

    owner = start_owner(&oa, mode, NULL, 0);
    expected = (long)g_m.futex;
    w.addr = &g_m.futex;
    w.op = FUTEX_PRIVATE | FUTEX_WAIT;
    w.val = (uint32_t)expected;
    w.arg4 = (unsigned long)&ts;
    w.ret = 12345;
    pthread_create(&waiter, NULL, waiter_fn, &w);
    wait_armed(1);

    oa.go = 1;
    pthread_join(owner, NULL);
    pthread_join(waiter, NULL);

    CHECK(oa.set_ret == 0, "set_robust_list succeeds");
    CHECK(w.ret == 0, wake_name);
    rc = (int)(g_m.futex);
    CHECK(rc == (int)(FUTEX_WAITERS | FUTEX_OWNER_DIED), word_name);
}

int main(void) {
    /* 1. lock in the list */
    run_case(M_LIST,
             "listed lock: waiter woken when owner exits",
             "listed lock: FUTEX_OWNER_DIED set, TID cleared");

    /* 2. lock only pending (half-acquired) */
    run_case(M_PENDING,
             "pending lock: waiter woken when owner exits",
             "pending lock: FUTEX_OWNER_DIED set, TID cleared");

    /* 3. lock both listed and pending: one cmpxchg, one wake */
    run_case(M_BOTH,
             "listed+pending lock: waiter woken once",
             "listed+pending: FUTEX_OWNER_DIED set once");

    /* 4. self-referential list must terminate */
    run_case(M_LOOP,
             "self-referential list: waiter still woken",
             "self-referential list: terminated with owner-died");

    /* 5. a listed lock owned by another tid must not be touched or woken */
    {
        struct owner_arg oa = { 0 };
        struct timespec ts;
        struct waiter w;
        pthread_t owner, waiter;
        uint32_t other = ((uint32_t)raw_gettid() + 7) & FUTEX_TID_MASK;
        if (other == 0)
            other = 1;

        rel_ts(&ts, 5000);
        g_armed = 0;
        g_done = 0;

        owner = start_owner(&oa, M_TWO, &g_m2, other);
        w.addr = &g_m2.futex;
        w.op = FUTEX_PRIVATE | FUTEX_WAIT;
        w.val = g_m2.futex;
        w.arg4 = (unsigned long)&ts;
        w.ret = 12345;
        pthread_create(&waiter, NULL, waiter_fn, &w);
        wait_armed(1);

        oa.go = 1;
        pthread_join(owner, NULL);
        usleep(150000);
        CHECK(g_m.futex == (FUTEX_WAITERS | FUTEX_OWNER_DIED),
              "two-lock list: owned lock marked owner-died");
        CHECK(g_m2.futex == (other | FUTEX_WAITERS),
              "two-lock list: other thread's lock untouched");
        CHECK(g_done == 0, "two-lock list: other thread's waiter not woken");
        long n = futex_sys(&g_m2.futex, FUTEX_PRIVATE | FUTEX_WAKE, 1, 0, NULL, 0);
        CHECK(n == 1, "two-lock list: manual wake releases the other waiter");
        pthread_join(waiter, NULL);
    }

    /* 6. wrong list length -> EINVAL */
    {
        long r = sys2(RB_SYS_SET_ROBUST_LIST, (unsigned long)&g_head, 16);
        CHECK(r == -EINVAL, "set_robust_list with wrong len is EINVAL");
    }

    /* 7. get_robust_list(pid) returns the registered head and 24 */
    {
        struct owner_arg oa = { 0 };
        pthread_t owner;
        void *hout = NULL;
        size_t lout = 0;
        long g;

        owner = start_owner(&oa, M_LIST, NULL, 0);
        g = sys3(RB_SYS_GET_ROBUST_LIST, (unsigned long)oa.owner_tid,
                 (unsigned long)&hout, (unsigned long)&lout);
        CHECK(g == 0 && hout == (void *)&g_head && lout == 24,
              "get_robust_list returns head and 24");
        oa.go = 1;
        pthread_join(owner, NULL);
    }

    /* 8. get_robust_list(unknown tid) -> ESRCH */
    {
        void *hout = NULL;
        size_t lout = 0;
        long g = sys3(RB_SYS_GET_ROBUST_LIST, 1000000UL,
                      (unsigned long)&hout, (unsigned long)&lout);
        CHECK(g == -ESRCH, "get_robust_list unknown tid is ESRCH");
    }

    /* 9. get_robust_list(0) reports the current thread (none registered) */
    {
        void *hout = (void *)1;
        size_t lout = 0;
        long g = sys3(RB_SYS_GET_ROBUST_LIST, 0,
                      (unsigned long)&hout, (unsigned long)&lout);
        CHECK(g == 0 && hout == NULL && lout == 24,
              "get_robust_list(0) reports no list for main thread");
    }

    printf(fails ? "ROBUST-FAIL: %d failures\n" : "ROBUST-OK\n", fails);
    return fails ? 1 : 0;
}
