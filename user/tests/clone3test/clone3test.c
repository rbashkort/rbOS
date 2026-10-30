/* clone3test — P3.5 clone3(435) MVP regression (plan.md P3.5).
 *
 * mlibc here is built without __MLIBC_LINUX_OPTION, so there is no public
 * clone3() helper; this file carries the Linux UAPI clone_args layout itself
 * and issues raw syscall 435. Covers:
 *   - a CLONE_THREAD thread (the glibc/musl pthread path) runs, writes
 *     CLONE_PARENT_SETTID/CLONE_CHILD_SETTID, and CLONE_CHILD_CLEARTID clears
 *     the tid on exit;
 *   - CLONE_SETTLS with the parent's fs_base;
 *   - fork-like clone3 (flags==0) reaped via waitpid, with exit_signal
 *     SIGCHLD and with exit_signal 0 (a blocking waitpid must still wake);
 *   - the size/version protocol (EINVAL < VER0, zero-padded larger struct
 *     accepted, non-zero tail and >PAGE_SIZE -> E2BIG);
 *   - error honesty: unknown/unsupported flags and a legacy signal byte ->
 *     EINVAL; pidfd requests and CLONE_PIDFD -> ENOSYS; exit_signal > 64 and
 *     a child stack outside user space -> EINVAL/EFAULT; bogus args -> EFAULT.
 *
 * The thread trampoline resumes right after `syscall` (kernel: create_user_thread
 * sets rsp = stack+stack_size, rax = 0), so it must pop {entry, arg} from the
 * prepared stack — exactly like mlibc's thread_entry.S. A plain C wrapper would
 * `ret` into the fresh stack.
 *
 * Prints "CLONE3-OK" on success, "CLONE3-FAIL: ..." (exit 1) on first failure.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- Linux clone3(2) UAPI (x86-64) ------------------------------------- */
struct clone_args {
    unsigned long long flags;
    unsigned long long pidfd;
    unsigned long long child_tid;
    unsigned long long parent_tid;
    unsigned long long exit_signal;
    unsigned long long stack;
    unsigned long long stack_size;
    unsigned long long tls;
};

#define CLONE_VM            0x00000100
#define CLONE_FS            0x00000200
#define CLONE_FILES         0x00000400
#define CLONE_SIGHAND       0x00000800
#define CLONE_PIDFD         0x00001000
#define CLONE_PARENT        0x00008000
#define CLONE_THREAD        0x00010000
#define CLONE_SYSVSEM       0x00040000
#define CLONE_SETTLS        0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID  0x01000000
#define CLONE_NEWUSER       0x10000000

#define SIGCHLD 17

#define NR_CLONE3       435
#define NR_FUTEX        202
#define NR_EXIT_THREAD  60
#define NR_ARCH_PRCTL   158

#define ARCH_GET_FS 0x1003

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static long raw6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8),
                       "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static long clone3_sys(struct clone_args *a, unsigned long size)
{
    return raw6(NR_CLONE3, (long)a, (long)size, 0, 0, 0, 0);
}

static void raw_exit_thread(int code)
{
    raw6(NR_EXIT_THREAD, code, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

static long futex_sys(int *uaddr, int op, int val)
{
    return raw6(NR_FUTEX, (long)uaddr, op, val, 0, 0, 0);
}

/* Thread trampoline: raw clone3, then in the child pop {entry, arg} off the
 * prepared stack, call entry(arg), and exit_thread. Mirrors mlibc's
 * thread_entry.S but for clone3 (rsi is the struct size, not a flag). */
__attribute__((naked)) static long clone3_thread_asm(
    void *args __attribute__((unused)), long size __attribute__((unused)))
{
    __asm__ volatile("mov $435, %eax\n\t"
                     "syscall\n\t"
                     "test %rax, %rax\n\t"
                     "jnz 1f\n\t"
                     "xor %ebp, %ebp\n\t"
                     "pop %rbx\n\t"
                     "pop %rdi\n\t"
                     "call *%rbx\n\t"
                     "mov %eax, %edi\n\t"
                     "mov $60, %eax\n\t"
                     "syscall\n\t"
                     "1:\n\t"
                     "ret\n\t");
}

static int g_fails;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("CLONE3-FAIL: %s (errno=%d)\n", (msg), errno);              \
            g_fails++;                                                         \
            errno = 0;                                                         \
        }                                                                      \
    } while (0)

/* Wait until *p != 0 / *p == 0, joined via futex (bounded against a missed
 * wake or a wedged child). */
static void wait_nonzero(volatile int *p)
{
    for (int i = 0; i < 200000; i++) {
        if (__atomic_load_n(p, __ATOMIC_ACQUIRE) != 0)
            return;
        futex_sys((int *)p, FUTEX_WAIT, 0);
    }
}

static void wait_zero(volatile int *p)
{
    for (int i = 0; i < 200000; i++) {
        int v = __atomic_load_n(p, __ATOMIC_ACQUIRE);
        if (v == 0)
            return;
        futex_sys((int *)p, FUTEX_WAIT, v);
    }
}

static volatile int g_thread_ran;
static volatile int g_thread2_ran;
static int g_parent_tid;
static int g_child_tid;
static int g_parent_tid2;
static int g_child_tid2;

static char g_thread_stack[64 * 1024] __attribute__((aligned(16)));
static char g_thread_stack2[64 * 1024] __attribute__((aligned(16)));

static void thread_body(void *arg)
{
    (void)arg;
    __atomic_store_n(&g_thread_ran, 1, __ATOMIC_RELEASE);
    futex_sys((int *)&g_thread_ran, FUTEX_WAKE, 1);
}

static void thread_body2(void *arg)
{
    (void)arg;
    __atomic_store_n(&g_thread2_ran, 1, __ATOMIC_RELEASE);
    futex_sys((int *)&g_thread2_ran, FUTEX_WAKE, 1);
}

/* Prepare a child stack holding {entry, arg} at its top and issue clone3.
 * Returns the child tid (>0) or a negative errno. */
static long spawn_thread_common(char *stack, unsigned long stacksz,
                                int thread_flags, int *parent_tid,
                                int *child_tid, unsigned long tls,
                                void (*entry)(void *))
{
    unsigned long top = (unsigned long)stack + stacksz;
    unsigned long *slots = (unsigned long *)(top - 16);
    struct clone_args ca;

    slots[0] = (unsigned long)entry;
    slots[1] = 0;

    memset(&ca, 0, sizeof ca);
    ca.flags = thread_flags;
    ca.parent_tid = (unsigned long long)(unsigned long)parent_tid;
    ca.child_tid = (unsigned long long)(unsigned long)child_tid;
    ca.exit_signal = 0;
    ca.stack = (unsigned long long)(unsigned long)stack;
    ca.stack_size = stacksz - 16; /* sp = stack + stack_size = top - 16 */
    ca.tls = (unsigned long long)tls;

    return clone3_thread_asm(&ca, (long)sizeof ca);
}

int main(void)
{
    errno = 0;

    /* 1. Thread: the CLONE_THREAD path every pthread_create takes. */
    errno = 0;
    long r = spawn_thread_common(
        g_thread_stack, sizeof g_thread_stack,
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
            CLONE_SYSVSEM | CLONE_PARENT_SETTID | CLONE_CHILD_SETTID |
            CLONE_CHILD_CLEARTID,
        &g_parent_tid, &g_child_tid, 0, thread_body);
    CHECK(r > 0, "clone3(CLONE_THREAD) creates a thread");
    if (r > 0) {
        wait_nonzero(&g_thread_ran);
        CHECK(__atomic_load_n(&g_thread_ran, __ATOMIC_ACQUIRE) == 1,
              "cloned thread ran");
        CHECK(g_parent_tid == (int)r, "CLONE_PARENT_SETTID wrote the tid");
        /* CLONE_CHILD_CLEARTID: the kernel clears it on thread exit. */
        wait_zero(&g_child_tid);
        CHECK(__atomic_load_n(&g_child_tid, __ATOMIC_ACQUIRE) == 0,
              "CLONE_CHILD_CLEARTID cleared the tid on exit");
    }

    /* 2. CLONE_SETTLS: inherit the parent's fs_base so the child stays valid. */
    unsigned long fs_base = 0;
    long pr = raw6(NR_ARCH_PRCTL, ARCH_GET_FS, (long)&fs_base, 0, 0, 0, 0);
    if (pr == 0) {
        errno = 0;
        r = spawn_thread_common(
            g_thread_stack2, sizeof g_thread_stack2,
            CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
                CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID |
                CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID,
            &g_parent_tid2, &g_child_tid2, fs_base, thread_body2);
        CHECK(r > 0, "clone3(CLONE_SETTLS) creates a thread");
        if (r > 0) {
            wait_nonzero(&g_thread2_ran);
            CHECK(__atomic_load_n(&g_thread2_ran, __ATOMIC_ACQUIRE) == 1,
                  "CLONE_SETTLS thread ran");
            CHECK(g_parent_tid2 == (int)r, "SETTLS thread tid published");
        }
    } else {
        /* Without ARCH_GET_FS we cannot pick a safe tls; skip this case. */
        printf("clone3test: arch_prctl(ARCH_GET_FS) unavailable, "
               "skipping CLONE_SETTLS case\n");
    }

    /* 3. Fork-like clone3 (flags==0) with exit_signal = SIGCHLD. */
    {
        struct clone_args ca;
        memset(&ca, 0, sizeof ca);
        ca.exit_signal = SIGCHLD;
        errno = 0;
        long pid = clone3_sys(&ca, sizeof ca);
        if (pid == 0)
            raw_exit_thread(42);
        CHECK(pid > 0, "clone3(fork-like, SIGCHLD) creates a process");
        if (pid > 0) {
            int st = 0;
            errno = 0;
            long w = waitpid((int)pid, &st, 0);
            CHECK(w == pid, "waitpid reaps the clone3 child");
            CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 42,
                  "clone3 child exit status is 42");
        }
    }

    /* 4. Fork-like clone3 with exit_signal = 0: the parent must still be
     *    woken so a blocking waitpid can reap (reaping is not tied to the
     *    notification signal). */
    {
        struct clone_args ca;
        memset(&ca, 0, sizeof ca);
        ca.exit_signal = 0;
        errno = 0;
        long pid = clone3_sys(&ca, sizeof ca);
        if (pid == 0)
            raw_exit_thread(7);
        CHECK(pid > 0, "clone3(fork-like, exit_signal=0) creates a process");
        if (pid > 0) {
            int st = 0;
            errno = 0;
            long w = waitpid((int)pid, &st, 0);
            CHECK(w == pid, "waitpid reaps an exit_signal=0 child");
            CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 7,
                  "exit_signal=0 child status is 7");
        }
    }

    /* 5. size/version protocol. */
    {
        struct clone_args ca;
        memset(&ca, 0, sizeof ca);

        errno = 0;
        CHECK(clone3_sys(&ca, 0) == -EINVAL, "size 0 -> EINVAL");
        errno = 0;
        CHECK(clone3_sys(&ca, 63) == -EINVAL, "size 63 -> EINVAL");

        /* size 64 accepted: a pidfd request then reports ENOSYS (proves the
         * struct was parsed). */
        ca.pidfd = 1;
        errno = 0;
        CHECK(clone3_sys(&ca, 64) == -ENOSYS, "size 64 accepted (pidfd -> ENOSYS)");

        /* Zero-padded VER2 (88 bytes) accepted. */
        memset(&ca, 0, sizeof ca);
        ca.pidfd = 1;
        errno = 0;
        CHECK(clone3_sys(&ca, 88) == -ENOSYS,
              "size 88 zero-padded accepted (pidfd -> ENOSYS)");

        /* A non-zero tail past VER0 is E2BIG. */
        unsigned char big[128];
        memset(big, 0, sizeof big);
        big[64] = 0xAA;
        errno = 0;
        CHECK(clone3_sys((struct clone_args *)big, sizeof big) == -E2BIG,
              "non-zero tail past VER0 -> E2BIG");

        /* Larger than a page is E2BIG. */
        errno = 0;
        CHECK(clone3_sys(&ca, 4097) == -E2BIG, "size 4097 -> E2BIG");

        /* Unmapped args pointer -> EFAULT. */
        errno = 0;
        CHECK(clone3_sys((struct clone_args *)0x1, 64) == -EFAULT,
              "bad args pointer -> EFAULT");
    }

    /* 6. Flag honesty. */
    {
        struct clone_args ca;

        /* Legacy signal byte must be zero in clone3. */
        memset(&ca, 0, sizeof ca);
        ca.flags = SIGCHLD;
        errno = 0;
        CHECK(clone3_sys(&ca, 64) == -EINVAL, "signal byte in flags -> EINVAL");

        /* Unsupported flags are refused, not ignored. */
        static const unsigned long long badflags[] = { CLONE_NEWUSER,
                                                       CLONE_PARENT };
        for (unsigned i = 0; i < sizeof badflags / sizeof badflags[0]; i++) {
            memset(&ca, 0, sizeof ca);
            ca.flags = badflags[i];
            errno = 0;
            CHECK(clone3_sys(&ca, 64) == -EINVAL,
                  "unsupported clone3 flag -> EINVAL");
        }

        /* pidfd requests/CLONE_PIDFD -> ENOSYS (whole call, not ignored). */
        memset(&ca, 0, sizeof ca);
        ca.flags = CLONE_PIDFD;
        errno = 0;
        CHECK(clone3_sys(&ca, 64) == -ENOSYS, "CLONE_PIDFD -> ENOSYS");
        memset(&ca, 0, sizeof ca);
        ca.pidfd = 1;
        errno = 0;
        CHECK(clone3_sys(&ca, 64) == -ENOSYS, "pidfd != 0 -> ENOSYS");
    }

    /* 7. exit_signal range and child-stack bounds. */
    {
        struct clone_args ca;

        memset(&ca, 0, sizeof ca);
        ca.exit_signal = 65;
        errno = 0;
        CHECK(clone3_sys(&ca, 64) == -EINVAL, "exit_signal 65 -> EINVAL");

        /* stack + stack_size must stay below user space. */
        memset(&ca, 0, sizeof ca);
        ca.exit_signal = SIGCHLD;
        ca.stack = 0x7fffffffe000ULL;
        ca.stack_size = 0x40000;
        errno = 0;
        CHECK(clone3_sys(&ca, 64) == -EFAULT, "stack past user space -> EFAULT");
    }

    if (g_fails == 0) {
        printf("CLONE3-OK\n");
        return 0;
    }
    printf("CLONE3-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
