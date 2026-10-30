/* stacktest: P2 (guard gap + ASLR) regression test.
 *
 * Subtests (plan.md P2 acceptance):
 *  1. 1.2 MiB page-touch recursion passes (proves lazy VMA growth works).
 *     Depth is self-checked: rsp must have moved >= 1.0 MiB, so a compiler
 *     that folds the recursion into one frame (tail-call optimization)
 *     fails closed instead of passing vacuously.
 *  2. deep recursion to ~7 MiB passes, canary + FS base intact.
 *  3. gap geometry via /usr/bin/stackgeo (pure _start, no libc: at entry FS
 *     is still the kernel-placed TLS/TCB below the stack reserve; any libc
 *     startup would mmap its own TLS and hide the geometry):
 *     (entry_rsp - fs_base) must be ~9M (8M reserve + 1M gap). The old
 *     1-page guard gives ~8M and fails here.
 *  4. child alloca(9M) from the top dies SIGSEGV (past the 8 MiB reserve).
 *  5. child fills the stack to ~7.1 MiB (depth self-checked), then walks
 *     2 MiB down page by page: must die SIGSEGV in the gap, never return
 *     (no TLS scribble).
 *  6. ASLR smoke: exec stackgeo 6 times, entry rsps must vary (>1 distinct)
 *     within a 0..1 MiB slide.
 *
 * Markers for rbtest: pass "STACK-ALL-PASS", fail "STACK-FAIL".
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MiB (1024u * 1024u)

#define RB_SYS_ARCH_PRCTL 158
#define ARCH_GET_FS 0x1003
#define SIGSEGV_NUM 11

#define STACKGEO "/usr/bin/stackgeo"

static volatile int g_canary = 42;

static long raw_syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

static int get_fs_base(uint64_t *out)
{
    *out = 0;
    long r = raw_syscall2(RB_SYS_ARCH_PRCTL, ARCH_GET_FS, (long)out);
    if (r != 0 || *out == 0)
        return -1;
    return 0;
}

static uint64_t cur_rsp(void)
{
    uint64_t sp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    return sp;
}

/* One 4 KiB stack frame per level, touched so the fault path must back it.
 * noinline keeps real call frames; the volatile read AFTER the recursive
 * call keeps it out of tail position (without it GCC folds the whole
 * recursion into a single reused frame and the depth proves nothing). */
__attribute__((noinline)) static void rec_touch(int depth, int target,
                                                uint64_t top_rsp,
                                                volatile int *sink)
{
    volatile char page[4096];
    page[0] = (char)depth;
    page[2048] = (char)(depth >> 3);
    page[4095] = (char)(depth >> 8);
    *sink += page[0] + page[2048] + page[4095];
    if (depth < target) {
        rec_touch(depth + 1, target, top_rsp, sink);
        *sink += page[0]; /* out of tail position: frame stays live */
        return;
    }
    /* At depth: self-check the consumed stack. */
    {
        uint64_t used = top_rsp - cur_rsp();
        if (used < (uint64_t)target * 3400u) {
            printf("STACK-FAIL: recursion folded? depth=%d used only %lu bytes\n",
                   target, (unsigned long)used);
            fflush(stdout);
            _exit(3);
        }
    }
}

/* Deep fill, then a 2 MiB page-by-page walk downward from the current rsp.
 * The walk first crosses the VMA tail (faults pages in, fine), then must hit
 * the unmapped guard gap and die SIGSEGV. Returning means the gap did not
 * catch us (TLS scribble or missing guard) -> caller reports failure. */
__attribute__((noinline)) static void rec_fill_then_walk(int depth, int target,
                                                         uint64_t top_rsp,
                                                         volatile int *sink)
{
    volatile char page[4096];
    page[0] = (char)depth;
    page[2048] = (char)(depth >> 3);
    page[4095] = (char)(depth >> 8);
    *sink += page[0] + page[2048] + page[4095];
    if (depth < target) {
        rec_fill_then_walk(depth + 1, target, top_rsp, sink);
        *sink += page[0]; /* out of tail position */
        return;
    }
    {
        uint64_t rsp = cur_rsp();
        uint64_t used = top_rsp - rsp;
        if (used < 7u * MiB) {
            printf("STACK-FAIL: walk fill too shallow (%lu bytes)\n",
                   (unsigned long)used);
            fflush(stdout);
            _exit(3);
        }
        {
            uint64_t lo = rsp - (2u * MiB);
            for (uint64_t a = rsp - 4096; a > lo; a -= 4096)
                *(volatile char *)a = (char)0x5A;
        }
    }
    /* Unreachable when the gap works. */
    printf("STACK-FAIL: deep 2M walk survived (gap missed/TLS scribbled)\n");
    fflush(stdout);
    _exit(2);
}

/* Run stackgeo, parse "GEO-RSP <hex>" and "GEO-FS <hex>". */
static int geo_probe(uint64_t *rsp_out, uint64_t *fs_out)
{
    int fds[2];
    if (pipe(fds) < 0)
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        close(fds[1]);
        execl(STACKGEO, STACKGEO, (char *)0);
        _exit(127);
    }
    close(fds[1]);
    char buf[256];
    size_t len = 0;
    for (;;) {
        ssize_t n = read(fds[0], buf + len, sizeof(buf) - 1 - len);
        if (n <= 0)
            break;
        len += (size_t)n;
        if (len >= sizeof(buf) - 1)
            break;
    }
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
        return -1;
    buf[len] = '\0';
    char *p = strstr(buf, "GEO-RSP ");
    char *q = strstr(buf, "GEO-FS ");
    if (!p || !q)
        return -1;
    *rsp_out = strtoul(p + 8, NULL, 16);
    *fs_out = strtoul(q + 7, NULL, 16);
    return (*rsp_out != 0 && *fs_out != 0) ? 0 : -1;
}

static int run_child_sigsegv(const char *what, void (*fn)(void))
{
    pid_t pid = fork();
    if (pid < 0) {
        printf("STACK-FAIL: %s fork failed\n", what);
        return -1;
    }
    if (pid == 0) {
        fn();
        /* fn must die by signal; reaching here means it survived. */
        printf("STACK-FAIL: %s survived (want SIGSEGV)\n", what);
        fflush(stdout);
        _exit(2);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV_NUM) {
        printf("stacktest: %s died SIGSEGV as expected\n", what);
        return 0;
    }
    if (WIFSIGNALED(st))
        printf("STACK-FAIL: %s died by signal %d (want SIGSEGV=11)\n", what,
               WTERMSIG(st));
    else if (WIFEXITED(st))
        printf("STACK-FAIL: %s exited %d (want SIGSEGV death)\n", what,
               WEXITSTATUS(st));
    else
        printf("STACK-FAIL: %s unknown wait status %d\n", what, st);
    return -1;
}

static void child_alloca_9m(void)
{
    size_t sz = 9u * MiB;
    volatile char *p = __builtin_alloca(sz);
    /* Touch low-to-high: p[0] is already past the 8 MiB reserve, in the
     * guard gap -> first touch faults. Walk on so a misplaced mapping
     * anywhere in range is still caught. */
    for (size_t i = 0; i < sz; i += 4096)
        p[i] = (char)0x5A;
    printf("child_alloca_9m: survived?! TLS/stack layout broken\n");
    fflush(stdout);
    _exit(2);
}

static void child_deep_2m_walk(void)
{
    volatile int sink = 0;
    /* 1820 levels x ~4 KiB ~= 7.1 MiB: rsp ends ~0.9 MiB above the VMA
     * bottom; the 2 MiB walk then crosses ~0.9 MiB of VMA tail and must
     * fault in the 1 MiB gap. Depth < 7M fails closed inside. */
    rec_fill_then_walk(0, 1820, cur_rsp(), &sink);
    _exit(2); /* only if the walk returned (failure, already printed) */
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    int fails = 0;
    volatile int sink = 0;

    /* 1. 1.2 MiB recursion (page-touch, not one frame). */
    g_canary = 42;
    rec_touch(0, 300, cur_rsp(), &sink);
    if (g_canary != 42) {
        printf("STACK-FAIL: canary clobbered after 1.2M recursion\n");
        fails++;
    } else {
        printf("stacktest: 1.2M recursion OK (sink=%d)\n", sink);
    }

    /* 2. deep recursion to ~7 MiB, FS base must not move. */
    uint64_t fs_before = 0;
    if (get_fs_base(&fs_before) != 0) {
        printf("STACK-FAIL: arch_prctl(GET_FS) failed\n");
        fails++;
    }
    g_canary = 42;
    rec_touch(0, 1700, cur_rsp(), &sink);
    {
        uint64_t fs_after = 0;
        int fs_ok = get_fs_base(&fs_after);
        if (g_canary != 42) {
            printf("STACK-FAIL: canary clobbered after 7M recursion\n");
            fails++;
        } else if (fs_ok != 0 || fs_after != fs_before) {
            printf("STACK-FAIL: FS moved over deep recursion (%lx -> %lx)\n",
                   (unsigned long)fs_before, (unsigned long)fs_after);
            fails++;
        } else {
            printf("stacktest: 7M recursion OK (sink=%d fs=%lx)\n", sink,
                   (unsigned long)fs_before);
        }
    }

    /* 3. gap geometry via the no-libc probe: entry_rsp - fs must show the
     * 8M reserve + 1M gap. Probe argv/env are fixed, so entry rsp sits a
     * few KiB below the randomized top and the TLS/TCB a page below the
     * gap: allow +-64K around 9M. The old 1-page guard gives ~8M and
     * fails here. */
    {
        uint64_t grsp = 0, gfs = 0;
        if (geo_probe(&grsp, &gfs) != 0) {
            printf("STACK-FAIL: stackgeo probe failed (missing %s?)\n",
                   STACKGEO);
            fails++;
        } else {
            uint64_t dist = grsp - gfs;
            printf("stacktest: geometry rsp=%lx fs=%lx dist=%lu (0x%lx)\n",
                   (unsigned long)grsp, (unsigned long)gfs,
                   (unsigned long)dist, (unsigned long)dist);
            if (dist < 9u * MiB - 64u * 1024u || dist > 9u * MiB + 64u * 1024u) {
                printf("STACK-FAIL: gap geometry off: want ~9M (8M reserve + 1M gap)\n");
                fails++;
            } else {
                printf("stacktest: gap geometry OK (1M guard)\n");
            }
        }
    }

    /* 4. alloca(9M) from the top: past the reserve -> SIGSEGV. */
    if (run_child_sigsegv("alloca-9M", child_alloca_9m) != 0)
        fails++;

    /* 5. fill to ~7.1M, walk 2M down page by page -> SIGSEGV in the gap. */
    if (run_child_sigsegv("deep-2M-walk", child_deep_2m_walk) != 0)
        fails++;

    /* 6. ASLR smoke: entry tops must vary across execs within 0..1M slide
     * (probe argv/env fixed, so spread == top spread). */
    {
        uint64_t rsps[6];
        int n = 0;
        for (int i = 0; i < 6; i++) {
            uint64_t r = 0, f = 0;
            if (geo_probe(&r, &f) != 0)
                break;
            rsps[i] = r;
            n++;
        }
        if (n != 6) {
            printf("STACK-FAIL: ASLR probe exec failed (%d/6)\n", n);
            fails++;
        } else {
            uint64_t mn = rsps[0], mx = rsps[0];
            for (int i = 1; i < 6; i++) {
                if (rsps[i] < mn)
                    mn = rsps[i];
                if (rsps[i] > mx)
                    mx = rsps[i];
            }
            int distinct = 0;
            for (int i = 1; i < 6; i++)
                if (rsps[i] != rsps[0])
                    distinct = 1;
            printf("stacktest: aslr spread=%lu distinct=%d\n",
                   (unsigned long)(mx - mn), distinct);
            if (!distinct) {
                printf("STACK-FAIL: stack top never randomized over 6 execs\n");
                fails++;
            } else if (mx - mn > MiB + 64u * 1024u) {
                printf("STACK-FAIL: stack slide exceeds 1M (+slack)\n");
                fails++;
            } else {
                printf("stacktest: ASLR slide OK\n");
            }
        }
    }

    if (fails == 0) {
        printf("STACK-ALL-PASS\n");
        return 0;
    }
    printf("STACK-FAIL: %d subtest(s) failed\n", fails);
    return 1;
}
