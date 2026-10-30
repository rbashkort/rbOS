/* rseqtest — P3.4 rseq(334) MVP regression (plan.md P3.4).
 *
 * mlibc here is built without __MLIBC_LINUX_OPTION (no <linux/rseq.h>), so
 * this file carries the UAPI layout and the x86-64 signature itself and issues
 * raw syscall 334. The MVP is a registration stub: register/unregister are
 * tracked per thread and validated like Linux, but the kernel neither
 * maintains `struct rseq.cpu_id` nor aborts critical sections (documented).
 * Covers:
 *   - register -> 0; identical re-register -> 0; conflicting -> EBUSY;
 *   - unregister -> 0; wrong sig -> EPERM; wrong len/ptr/flags -> EINVAL;
 *     unregister when nothing is registered -> EINVAL;
 *   - register: flags != 0 -> EINVAL, len != 32 -> EINVAL, misaligned ->
 *     EINVAL, NULL / out-of-range -> EFAULT;
 *   - registration does not touch the user struct (cpu_id sentinel unchanged);
 *   - per-thread state: a forked child has no inherited registration and can
 *     register independently while the parent's registration stays valid.
 *
 * Prints "RSEQ-OK" on success, "RSEQ-FAIL: ..." (exit 1) on first failure.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- Linux rseq(2) UAPI (x86-64) --------------------------------------- */
struct rseq {
    unsigned int cpu_id_start;
    unsigned int cpu_id;
    unsigned long long rseq_cs;
    unsigned int flags;
    unsigned int node_id;
    unsigned int mm_cid;
} __attribute__((aligned(32)));

#define RSEQ_SIG 0x53053053u
#define RSEQ_FLAG_UNREGISTER 1

#define NR_RSEQ 334

static long rseq_sys(struct rseq *r, unsigned int len, int flags, unsigned int sig)
{
    long ret;
    register long r10 __asm__("r10") = (long)sig;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"((long)NR_RSEQ), "D"((long)r), "S"((long)len),
                       "d"((long)flags), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static int g_fails;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("RSEQ-FAIL: %s (errno=%d)\n", (msg), errno);                \
            g_fails++;                                                         \
            errno = 0;                                                         \
        }                                                                      \
    } while (0)

static struct rseq g_area;
static struct rseq g_other;

int main(void)
{
    errno = 0;

    /* 1. Register, then prove we do not write the user struct. */
    g_area.cpu_id = 0xDEADBEEFu;
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, 0, RSEQ_SIG) == 0, "register -> 0");
    CHECK(g_area.cpu_id == 0xDEADBEEFu,
          "registration leaves cpu_id untouched (stub: not maintained)");

    /* 2. Identical re-registration is idempotent; a different area is EBUSY. */
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, 0, RSEQ_SIG) == 0, "re-register (same) -> 0");
    errno = 0;
    CHECK(rseq_sys(&g_other, 32, 0, RSEQ_SIG) == -EBUSY,
          "re-register (different ptr) -> EBUSY");

    /* 3. Unregistration validation. */
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, RSEQ_FLAG_UNREGISTER, RSEQ_SIG ^ 1u) == -EPERM,
          "unregister wrong sig -> EPERM");
    errno = 0;
    CHECK(rseq_sys(&g_area, 31, RSEQ_FLAG_UNREGISTER, RSEQ_SIG) == -EINVAL,
          "unregister wrong len -> EINVAL");
    errno = 0;
    CHECK(rseq_sys(&g_other, 32, RSEQ_FLAG_UNREGISTER, RSEQ_SIG) == -EINVAL,
          "unregister wrong ptr -> EINVAL");
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, RSEQ_FLAG_UNREGISTER | 2, RSEQ_SIG) == -EINVAL,
          "unregister extra flags -> EINVAL");

    /* 4. Real unregistration, then a second one must fail. */
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, RSEQ_FLAG_UNREGISTER, RSEQ_SIG) == 0,
          "unregister -> 0");
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, RSEQ_FLAG_UNREGISTER, RSEQ_SIG) == -EINVAL,
          "unregister when none -> EINVAL");

    /* 5. Registration validation. */
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, 2, RSEQ_SIG) == -EINVAL,
          "register flags != 0 -> EINVAL");
    errno = 0;
    CHECK(rseq_sys(&g_area, 31, 0, RSEQ_SIG) == -EINVAL, "register len 31 -> EINVAL");
    errno = 0;
    CHECK(rseq_sys(&g_area, 64, 0, RSEQ_SIG) == -EINVAL, "register len 64 -> EINVAL");
    errno = 0;
    CHECK(rseq_sys((struct rseq *)((char *)&g_area + 8), 32, 0, RSEQ_SIG) == -EINVAL,
          "misaligned area -> EINVAL");
    errno = 0;
    CHECK(rseq_sys(NULL, 32, 0, RSEQ_SIG) == -EFAULT, "NULL area -> EFAULT");
    errno = 0;
    CHECK(rseq_sys((struct rseq *)0x0000800000000000ULL, 32, 0, RSEQ_SIG) == -EFAULT,
          "out-of-range area -> EFAULT");

    /* 6. rseq state is per thread: a fork clears the registration in the child,
     *    which can then register its own area, while the parent's survives. */
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, 0, RSEQ_SIG) == 0, "register (parent, before fork)");
    errno = 0;
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: fresh thread state, so this is a first registration. */
        long r1 = rseq_sys(&g_other, 32, 0, RSEQ_SIG);
        long r2 = rseq_sys(&g_other, 32, RSEQ_FLAG_UNREGISTER, RSEQ_SIG);
        _exit((r1 == 0 && r2 == 0) ? 0 : 5);
    }
    CHECK(pid > 0, "fork for per-thread rseq test");
    if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "child has independent (cleared then re-registered) rseq");
    }
    /* Parent's registration was not disturbed by the fork. */
    errno = 0;
    CHECK(rseq_sys(&g_area, 32, RSEQ_FLAG_UNREGISTER, RSEQ_SIG) == 0,
          "parent registration survives fork");

    if (g_fails == 0) {
        printf("RSEQ-OK\n");
        return 0;
    }
    printf("RSEQ-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
