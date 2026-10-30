/* rbtest: non-interactive test runner for rbOS.
 *
 * Runs every userspace *test binary without needing a terminal (no stdin),
 * isolates each test in a child with a pipe-captured stdout/stderr and a
 * per-test timeout, then prints a machine-parseable report to stdout
 * (which mirrors to serial, so `qemu -serial file:log` captures it even
 * when the framebuffer is dead) and to /tmp + /home report files.
 *
 * Markers for the host-side script (tools/rbtest-host.sh):
 *   >>> RBTEST BEGIN suite=N <<<
 *   >>> RBTEST RUN <name> <<<
 *   >>> RBTEST RESULT <name> <PASS|FAIL|SKIP|TIMEOUT> exit=.. time=..s note=.. <<<
 *   >>> RBTEST SUMMARY passed=.. failed=.. skipped=.. timeout=.. <<<
 *   >>> RBTEST DONE rc=.. <<<
 *
 * A kernel crash (e.g. an SMP deadlock, or the historical fbtest
 * page-fault in ring0 — fixed)
 * kills the runner itself: no DONE marker appears and the host script
 * attributes the crash/hang to the last RUN test. That is by design —
 * in-guest isolation covers userspace faults, host timeouts cover kernel
 * faults.
 *
 * Exit code: 0 when failed==0 && timeout==0, else 1. When PID 1 (autotest
 * via `rbtest` on the kernel cmdline) the runner parks forever after DONE
 * so the host can kill QEMU deterministically instead of racing PID1 exit.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define OUT_CAP (64 * 1024)
#define DEFAULT_TIMEOUT 15

static char g_outbuf[OUT_CAP + 1];

struct test_desc {
    const char *name;          /* short name for markers */
    const char *path;          /* binary to exec */
    const char *arg;           /* optional single arg (crashtest mode) */
    int timeout_sec;           /* per-test watchdog */
    int expect_signal;         /* 1: death by signal == PASS (crashtest) */
    int interactive;           /* 1: needs a tty / human (skipped in batch) */
    const char *pass_mark;     /* must appear in output, else FAIL (may be NULL) */
    const char *fail_mark;     /* if appears in output -> FAIL even if exit 0 */
    const char *env_filter;    /* set JOBTEST_ONLY=<this> in the child (jobtest) */
    const char *skip_reason;   /* non-NULL: skip in full-suite runs (explicit name still runs it) */
};

/* Suite order matters: fbtest last — it used to take the whole machine
 * down (ring0 page fault in sys_munmap, fixed; plus a stack-smashing fbtest
 * binary, replaced). Keeping it last costs nothing and keeps any future
 * framebuffer regression from hiding the rest behind a missing DONE marker.
 * jobtest is split per JOBTEST_ONLY case
 * so one wedged stop/CONT subtest cannot hide the rest behind a single
 * timeout (and the serial log shows exactly which case hung). */
static const struct test_desc g_tests[] = {
    { "abitest",      "/usr/bin/abitest",    NULL, 15, 0, 0, "ALL PASS", "FAIL:", NULL, NULL },
    { "syscalltest",  "/usr/bin/syscalltest",NULL, 10, 0, 0, NULL,       "[FAIL]", NULL, NULL },
    { "devtest",      "/usr/bin/devtest",    NULL, 10, 0, 0, NULL,       "FAIL", NULL, NULL },
    { "fstest",       "/usr/bin/fstest",     NULL, 20, 0, 0, "ALL PASS", "FAIL:", NULL, NULL },
    { "memtest",      "/usr/bin/memtest",    NULL, 30, 0, 0, "PASS",     "FAIL", NULL, NULL },
    /* P5: shared fd/node pool capacity (600 > old 512) + VMA fit probe. */
    { "fdtest",       "/usr/bin/fdtest",     NULL, 30, 0, 0, "FDTEST-OK", "FDTEST-FAIL", NULL, NULL },
    { "pthreadtest",  "/usr/bin/pthreadtest",NULL, 20, 0, 0, "ALL PASS", "FAIL:", NULL, NULL },
    /* P4.1 futex(2) ABI: WAIT/WAKE, *_BITSET masks, absolute/relative
     * timeouts, REQUEUE/CMP_REQUEUE (raw syscall 202). */
    { "futextest",    "/usr/bin/futextest",  NULL, 20, 0, 0, "FUTEX-OK", "FUTEX-FAIL", NULL, NULL },
    /* P4.2 robust futex lists: owner-died + wake at thread exit, pending
     * lock, get_robust_list (raw syscalls 273/274). */
    { "robusttest",   "/usr/bin/robusttest", NULL, 20, 0, 0, "ROBUST-OK", "ROBUST-FAIL", NULL, NULL },
    /* Host-libc static TLS smoke test (user/tltest, gcc -static): runs the
     * kernel through a foreign libc's startup + static PT_TLS. Missing
     * binary (host without static libc) verdicts as SKIP, not FAIL. */
    { "tltest",       "/usr/bin/tltest",    NULL, 10, 0, 0, "TLS-OK",     "TLS-FAIL", NULL, NULL },
    /* Host-built Linux/glibc probes (host-tests/, opt-in). Not installed by
     * default: the exec-fail heuristic turns them into SKIP, so a plain
     * `make test` stays green. `make host-tests` installs them. */
    { "host-hello",   "/home/hosttests/hello",   NULL, 10, 0, 0, "HELLO-OK",   NULL, NULL, NULL },
    { "host-pthread", "/home/hosttests/pthread", NULL, 20, 0, 0, "PTHREAD-OK", NULL, NULL, NULL },
    { "host-tls",     "/home/hosttests/tls",     NULL, 10, 0, 0, "TLS-OK",     NULL, NULL, NULL },
    { "host-cxx",     "/home/hosttests/cxx",     NULL, 15, 0, 0, "CXX-OK",     NULL, NULL, NULL },
    /* P2 stack guard-gap + ASLR regression (static binary, needs no tty). */
    { "stacktest",    "/usr/bin/stacktest", NULL, 30, 0, 0, "STACK-ALL-PASS", "STACK-FAIL", NULL, NULL },
    { "jobtest-pipe", "/usr/bin/jobtest",    NULL, 10, 0, 0, NULL,       "FAIL", "pipe", NULL },
    { "jobtest-tstp", "/usr/bin/jobtest",    NULL, 10, 0, 0, NULL,       "FAIL", "tstp", NULL },
    { "jobtest-stop", "/usr/bin/jobtest",    NULL, 10, 0, 0, NULL,       "FAIL", "stop", NULL },
    { "jobtest-ttin", "/usr/bin/jobtest",    NULL, 10, 0, 0, NULL,       "FAIL", "ttin", NULL },
    { "jobtest-ttou", "/usr/bin/jobtest",    NULL, 10, 0, 0, NULL,       "FAIL", "ttou", NULL },
    { "jobtest-kill", "/usr/bin/jobtest",    NULL, 10, 0, 0, NULL,       "FAIL", "kill", NULL },
    { "jobtest-caught", "/usr/bin/jobtest",  NULL, 10, 0, 0, NULL,       "FAIL", "caught", NULL },
    { "jobtest-ignored", "/usr/bin/jobtest", NULL, 10, 0, 0, NULL,       "FAIL", "ignored", NULL },
    /* bgread: background read(0) must stop with SIGTTIN. Used to wedge the
     * whole machine (serial-console ctty mismatch + kill(pid>0,SIGKILL)
     * hitting the caller's group); both fixed, runs in the suite now. */
    { "jobtest-bgread", "/usr/bin/jobtest",  NULL, 10, 0, 0, NULL,       "FAIL", "bgread",
      NULL },
    { "jobtest-orphan", "/usr/bin/jobtest",  NULL, 10, 0, 0, NULL,       "FAIL", "orphan", NULL },
    { "setuidtest",   "/usr/bin/setuidtest", NULL, 10, 0, 0, "ALL PASS", "FAIL", NULL, NULL },
    /* P3.1 prctl(2): dumpable/name/set_vma subset + EINVAL for the rest. */
    { "prctltest",    "/usr/bin/prctltest",  NULL, 10, 0, 0, "PRCTL-OK",  "PRCTL-FAIL", NULL, NULL },
    /* P3.2 statx(2): BASIC_STATS vs stat(2), NOFOLLOW/EMPTY_PATH, error paths. */
    { "statxtest",    "/usr/bin/statxtest",  NULL, 10, 0, 0, "STATX-OK",  "STATX-FAIL", NULL, NULL },
    /* P3.7 procfs: /proc/<pid>/cmdline NUL-separated argv (BUG-001) and comm
     * served raw (BUG-002). */
    { "proctest",     "/usr/bin/proctest",   NULL, 10, 0, 0, "PROC-OK",   "PROC-FAIL", NULL, NULL },
    /* P3.3 openat2(2): thin openat wrapper, open_how size/version, resolve
     * honesty, O_CLOEXEC, error paths. */
    { "openat2test",  "/usr/bin/openat2test", NULL, 10, 0, 0, "OPENAT2-OK", "OPENAT2-FAIL", NULL, NULL },
    /* P3.5 clone3(2): thread path, SETTLS, fork-like + exit_signal, size
     * protocol, flag/pidfd honesty. */
    { "clone3test",   "/usr/bin/clone3test",  NULL, 10, 0, 0, "CLONE3-OK", "CLONE3-FAIL", NULL, NULL },
    /* P3.4 rseq(2): registration stub — register/unregister validation,
     * per-thread state across fork. */
    { "rseqtest",     "/usr/bin/rseqtest",    NULL, 10, 0, 0, "RSEQ-OK",   "RSEQ-FAIL", NULL, NULL },
    /* P3.6 linux option: mlibc-provided prctl()/statx()/getentropy()/
     * getrandom() wrappers + net-if ENOSYS without rtnetlink. */
    { "linuxtest",    "/usr/bin/linuxtest",   NULL, 10, 0, 0, "LINUX-OK",  "LINUX-FAIL", NULL, NULL },
    /* P7.b pledge(2): positive rpath/stdout, fatal wpath violations,
     * monotonic reduction, empty mask, and variant-C exec inheritance. */
    { "pledgetest",   "/usr/bin/pledgetest",  NULL, 20, 0, 0, "PLEDGE-OK", "PLEDGE-FAIL", NULL, NULL },
    /* P7.a userland arc4random(3): getentropy-backed, no kernel additions. */
    { "randomtest",   "/usr/bin/randomtest",  NULL, 10, 0, 0, "RANDOM-OK", "RANDOM-FAIL", NULL, NULL },
    /* P7.0b standard headers: ordinary view has no OpenBSD names; the
     * opt-in compat probe is compiled in the same test directory. */
    { "headertest",   "/usr/bin/headertest",  NULL, 5,  0, 0, "HEADER-OK", "HEADER-FAIL", NULL, NULL },
    /* P7.d unveil(2): visibility, r/w/x/c bits, longest prefix, fork/exec
     * inheritance and the NULL,NULL lock. */
    { "unveiltest",   "/usr/bin/unveiltest",  NULL, 20, 0, 0, "UNVEIL-OK", "UNVEIL-FAIL", NULL, NULL },
    /* P7.c sysctl: must be before crashtest — it raises securelevel to 2
     * and cannot lower it. No later test may depend on ASLR being off. */
    { "sysctltest",   "/usr/bin/sysctltest",  NULL, 10, 0, 0, "SYSCTL-OK", "SYSCTL-FAIL", NULL, NULL },
    { "crashtest-0",  "/usr/bin/crashtest",  "0",  10, 1, 0, NULL,       NULL, NULL, NULL },
    { "crashtest-1",  "/usr/bin/crashtest",  "1",  10, 1, 0, NULL,       NULL, NULL, NULL },
    { "crashtest-2",  "/usr/bin/crashtest",  "2",  10, 1, 0, NULL,       NULL, NULL, NULL },
    { "true-smoke",   "/usr/bin/true",       NULL, 5,  0, 0, NULL,       NULL, NULL, NULL },
    /* fetch has no /proc/hwinfo fallback: require at least one rendered field. */
    { "fetch-smoke",  "/usr/bin/fetch",      NULL, 5,  0, 0, "OS:",       NULL, NULL, NULL },
    /* fbtest LAST: historically it page-faulted in ring0 (vmm index typo
     * in unmap_no_shootdown, fixed) and took the whole machine down with no
     * DONE marker — everything else must have reported already so the host
     * can attribute a crash. Order kept as insurance. */
    { "fbtest",       "/usr/bin/fbtest",     NULL, 15, 0, 0, "successfully", NULL, NULL, NULL },
    /* Interactive: need a human on the VT (Enter key, visible switch). */
    { "vttest",       "/usr/bin/vttest",     NULL, 10, 0, 1, "PASS",     "failed", NULL, NULL },
};
#define NTESTS ((int)(sizeof(g_tests) / sizeof(g_tests[0])))

static const char *g_only = NULL;   /* RBTEST_ONLY=name filter */
static int g_force_interactive = 0; /* --interactive */
static int g_timeout_scale = 1;

static int has_substr(const char *hay, const char *needle)
{
    if (!needle || !needle[0])
        return 0;
    return strstr(hay, needle) != NULL;
}

/* Drain fd (nonblocking) into g_outbuf. Returns bytes appended. */
static size_t drain_fd(int fd, size_t *len)
{
    char tmp[2048];
    size_t added = 0;
    for (;;) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            size_t room = OUT_CAP - *len;
            size_t cp = (size_t)n < room ? (size_t)n : room;
            if (cp > 0) {
                memcpy(g_outbuf + *len, tmp, cp);
                *len += cp;
            }
            added += (size_t)n;
            if ((size_t)n < sizeof(tmp))
                break;
        } else if (n == 0) {
            break; /* EOF */
        } else {
            if (errno == EAGAIN || errno == EINTR)
                break;
            break;
        }
    }
    return added;
}

struct run_result {
    int timed_out;
    int unreaped;    /* SIGKILLed but waitpid never reaped it: kernel wedged? */
    int exited;
    int exit_code;   /* valid if exited && WIFEXITED */
    int signaled;    /* valid if exited && WIFSIGNALED */
    int termsig;     /* valid if signaled */
    int exec_failed; /* child could not exec (127 / ENOENT) */
    double secs;
};

/* Run one test, capture output into g_outbuf (NUL-terminated, *out_len set).
 * Child output is ALSO streamed to our stdout live: if the test wedges the
 * kernel (SMP deadlock) or itself, the serial log still shows exactly how
 * far it got — otherwise a hang would leave zero evidence behind.
 * No stdin interaction: child inherits stdin but must not need it
 * (interactive tests are skipped before we get here). */
static struct run_result run_one(const struct test_desc *t, size_t *out_len)
{
    struct run_result r;
    memset(&r, 0, sizeof(r));
    *out_len = 0;
    g_outbuf[0] = '\0';

    int fds[2];
    if (pipe(fds) < 0) {
        printf(">>> RBTEST RESULT %s FAIL exit=-1 time=0s note=pipe failed: %s <<<\n",
               t->name, strerror(errno));
        fflush(stdout);
        r.exited = 1;
        r.exit_code = 127;
        return r;
    }

    time_t t0 = time(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        printf(">>> RBTEST RESULT %s FAIL exit=-1 time=0s note=fork failed: %s <<<\n",
               t->name, strerror(errno));
        fflush(stdout);
        close(fds[0]);
        close(fds[1]);
        r.exited = 1;
        r.exit_code = 127;
        return r;
    }
    if (pid == 0) {
        /* Child: stdout+stderr -> pipe, then exec. */
        close(fds[0]);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        if (t->env_filter)
            setenv("JOBTEST_ONLY", t->env_filter, 1);
        if (t->arg) {
            execl(t->path, t->path, t->arg, (char *)0);
        } else {
            execl(t->path, t->path, (char *)0);
        }
        /* exec failed: report errno through the pipe (stdout is the pipe),
         * then exit 127 so the parent can tell exec-fail from real FAIL. */
        printf("rbtest: exec %s failed: %s\n", t->path, strerror(errno));
        fflush(stdout);
        _exit(127);
    }

    close(fds[1]);
    {
        int fl = fcntl(fds[0], F_GETFL, 0);
        if (fl >= 0)
            fcntl(fds[0], F_SETFL, fl | O_NONBLOCK);
    }

    int timeout = t->timeout_sec * g_timeout_scale;
    if (timeout < 1)
        timeout = 1;
    int status = 0;
    int done = 0;
    /* Poll every 100ms: drain pipe (avoid 4K pipe stall) + WNOHANG wait,
     * streaming each chunk to our own stdout immediately.
     * No SIGALRM/alarm on rbOS yet, so polling like jobtest does. */
    printf("--- output %s (live) ---\n", t->name);
    for (int tick = 0; tick < timeout * 10 && !done; tick++) {
        size_t before = *out_len;
        drain_fd(fds[0], out_len);
        if (*out_len > before) {
            fwrite(g_outbuf + before, 1, *out_len - before, stdout);
            fflush(stdout);
        }
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            done = 1;
            break;
        }
        usleep(100000);
    }
    if (!done) {
        /* Watchdog: still alive after timeout -> SIGKILL (SMP hang case:
         * the test wedged the scheduler or itself; ^C would not help
         * either since the console owner may be stuck). */
        printf("\nrbtest: watchdog: %s timed out after %ds, SIGKILL...\n",
               t->name, timeout);
        fflush(stdout);
        kill(pid, SIGKILL);
        /* Bounded reap: 5s of WNOHANG polling, then GIVE UP reaping and
         * continue with the next test. A blocking waitpid here would park
         * the whole suite forever when the kernel itself is wedged (e.g.
         * a stop/SIGKILL/waitpid deadlock in job control): the zombie is
         * leaked deliberately so the report — and the remaining tests —
         * still happen while the scheduler is alive. */
        for (int i = 0; i < 50; i++) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                done = 1;
                break;
            }
            usleep(100000);
        }
        if (!done) {
            printf("rbtest: watchdog: %s unreapable after SIGKILL, "
                   "leaking it and continuing (kernel may be wedged)\n",
                   t->name);
            fflush(stdout);
            status = 0;
            r.unreaped = 1;
        }
        r.timed_out = 1;
    }
    /* Final drain until EOF (streamed too). */
    for (int i = 0; i < 10; i++) {
        size_t before = *out_len;
        drain_fd(fds[0], out_len);
        if (*out_len > before) {
            fwrite(g_outbuf + before, 1, *out_len - before, stdout);
            fflush(stdout);
        }
        if (*out_len == before)
            break;
    }
    close(fds[0]);
    g_outbuf[*out_len] = '\0';
    printf("--- end output %s (%u bytes) ---\n", t->name, (unsigned)*out_len);

    time_t t1 = time(NULL);
    r.secs = (double)(t1 - t0);
    if (r.secs < 0)
        r.secs = 0;

    if (WIFEXITED(status)) {
        r.exited = 1;
        r.exit_code = WEXITSTATUS(status);
        /* exec-fail heuristic: exit 127 + our marker in output. */
        if (r.exit_code == 127 && has_substr(g_outbuf, "rbtest: exec"))
            r.exec_failed = 1;
    } else if (WIFSIGNALED(status)) {
        r.exited = 1;
        r.signaled = 1;
        r.termsig = WTERMSIG(status);
    } else if (!r.unreaped) {
        r.exited = 1;
        r.exit_code = 1;
    }
    /* unreaped: status is meaningless (child still out there); the
     * timed_out verdict below reports it. */
    return r;
}

static const char *verdict_one(const struct test_desc *t, struct run_result *r,
                               size_t out_len, char *note, size_t notesz)
{
    (void)out_len;
    note[0] = '\0';
    if (r->timed_out) {
        if (r->unreaped)
            snprintf(note, notesz,
                     "timeout after %ds, SIGKILL ignored/unreapable (kernel wedged?)",
                     t->timeout_sec);
        else
            snprintf(note, notesz, "timeout after %ds, killed", t->timeout_sec);
        return "TIMEOUT";
    }
    if (r->exec_failed) {
        snprintf(note, notesz, "binary missing or not executable");
        return "SKIP";
    }
    if (t->expect_signal) {
        /* crashtest: the kernel must kill THIS process (SIGSEGV) and stay
         * alive. Survival is proven by the fact we are still here. */
        if (r->signaled && (r->termsig == 11 || r->termsig == 4 || r->termsig == 7)) {
            snprintf(note, notesz, "killed by signal %d as expected, system survived",
                     r->termsig);
            return "PASS";
        }
        if (r->signaled) {
            snprintf(note, notesz, "killed by signal %d (want SIGSEGV=11)", r->termsig);
            return "FAIL";
        }
        snprintf(note, notesz, "survived (exit %d), want a crash", r->exit_code);
        return "FAIL";
    }
    if (r->signaled) {
        snprintf(note, notesz, "killed by signal %d", r->termsig);
        return "FAIL";
    }
    /* fail_mark wins over exit code: devtest/syscalltest return 0 even
     * when individual checks print FAIL. */
    if (t->fail_mark && has_substr(g_outbuf, t->fail_mark)) {
        snprintf(note, notesz, "exit %d but output contains '%s'", r->exit_code, t->fail_mark);
        return "FAIL";
    }
    if (t->pass_mark && !has_substr(g_outbuf, t->pass_mark)) {
        snprintf(note, notesz, "exit %d but missing '%s'", r->exit_code, t->pass_mark);
        return "FAIL";
    }
    if (r->exit_code != 0) {
        snprintf(note, notesz, "exit %d", r->exit_code);
        return "FAIL";
    }
    snprintf(note, notesz, "exit 0");
    return "PASS";
}

static void usage(const char *argv0)
{
    printf("usage: %s [--list] [--interactive] [--timeout-scale N] [name...]\n", argv0);
    printf("  no args: run the whole suite except interactive tests\n");
    printf("  RBTEST_ONLY=name env also filters to one test\n");
}

int main(int argc, char *argv[])
{
    int list_only = 0;
    /* Unbuffered so every marker hits serial immediately — if the next
     * test takes the kernel down, the log still shows who was running. */
    setvbuf(stdout, NULL, _IONBF, 0);

    const char *filter_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list") || !strcmp(argv[i], "-l")) {
            list_only = 1;
        } else if (!strcmp(argv[i], "--interactive")) {
            g_force_interactive = 1;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--timeout-scale") && i + 1 < argc) {
            g_timeout_scale = atoi(argv[++i]);
            if (g_timeout_scale < 1)
                g_timeout_scale = 1;
        } else if (argv[i][0] == '-') {
            printf("rbtest: unknown option %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else {
            filter_arg = argv[i];
            g_only = argv[i];
        }
    }
    {
        const char *e = getenv("RBTEST_ONLY");
        if (e && e[0] && !filter_arg)
            g_only = e;
    }
    {
        const char *e = getenv("RBTEST_INTERACTIVE");
        if (e && e[0] == '1')
            g_force_interactive = 1;
    }

    if (list_only) {
        for (int i = 0; i < NTESTS; i++)
            printf("%s%s\n", g_tests[i].name, g_tests[i].interactive ? " (interactive)" : "");
        return 0;
    }

    int is_tty = isatty(0);
    printf(">>> RBTEST BEGIN suite=%d tty=%d <<<\n", NTESTS, is_tty);

    int passed = 0, failed = 0, skipped = 0, timeouts = 0;
    /* Keep a text report in memory? No — stream to stdout AND append to
     * files at the end (files may live on ramfs/ext2; stdout always works). */
    FILE *report = fopen("/tmp/rbtest-report.txt", "w");
    if (report) {
        fprintf(report, "rbOS test report\n");
        fflush(report);
    }

    for (int i = 0; i < NTESTS; i++) {
        const struct test_desc *t = &g_tests[i];
        if (g_only && strcmp(g_only, t->name) != 0)
            continue;
        /* Explicit `rbtest <name>` (or RBTEST_ONLY) runs even
         * default-skipped tests: the user asked for exactly this one,
         * presumably in a throwaway VM. */
        int explicit = (g_only && strcmp(g_only, t->name) == 0);
        if (t->skip_reason && !explicit) {
            printf(">>> RBTEST RUN %s <<<\n", t->name);
            printf(">>> RBTEST RESULT %s SKIP exit=-1 time=0s note=%s <<<\n",
                   t->name, t->skip_reason);
            if (report)
                fprintf(report, "SKIP %s (%s)\n", t->name, t->skip_reason);
            skipped++;
            continue;
        }
        if (t->interactive && !g_force_interactive) {
            printf(">>> RBTEST RUN %s <<<\n", t->name);
            printf(">>> RBTEST RESULT %s SKIP exit=-1 time=0s note=needs tty, use --interactive <<<\n",
                   t->name);
            if (report)
                fprintf(report, "SKIP %s (needs tty)\n", t->name);
            skipped++;
            continue;
        }
        printf(">>> RBTEST RUN %s <<<\n", t->name);
        size_t out_len = 0;
        struct run_result r = run_one(t, &out_len);
        /* Output was already streamed live inside run_one; the verdict
         * below is driven by the full captured buffer. */
        char note[160];
        const char *v = verdict_one(t, &r, out_len, note, sizeof(note));
        int ex = r.signaled ? (128 + r.termsig) : r.exit_code;
        if (r.timed_out)
            ex = 124;
        if (r.exec_failed)
            ex = -1;
        printf(">>> RBTEST RESULT %s %s exit=%d time=%.0fs note=%s <<<\n",
               t->name, v, ex, r.secs, note);
        if (report) {
            fprintf(report, "%s %s (exit=%d, %.0fs) %s\n", v, t->name, ex, r.secs, note);
            fflush(report);
        }
        if (!strcmp(v, "PASS"))
            passed++;
        else if (!strcmp(v, "SKIP"))
            skipped++;
        else if (!strcmp(v, "TIMEOUT")) {
            timeouts++;
            failed++;
        } else
            failed++;
        /* ext2 persistence point: if the NEXT test crashes the kernel,
         * /tmp (ramfs) is gone but the serial log survives. The host
         * script is the source of truth after a kernel crash. */
    }

    printf(">>> RBTEST SUMMARY passed=%d failed=%d skipped=%d timeout=%d <<<\n",
           passed, failed, skipped, timeouts);
    int rc = (failed == 0) ? 0 : 1;
    printf(failed ? "rbtest: %d FAILED (%d timeout), %d passed, %d skipped\n"
                  : "rbtest: ALL PASS (%d passed, %d skipped)\n",
           failed, timeouts, passed, skipped);
    printf(">>> RBTEST DONE rc=%d <<<\n", rc);
    fflush(stdout);

    if (report) {
        fprintf(report, "summary: passed=%d failed=%d skipped=%d timeout=%d rc=%d\n",
                passed, failed, skipped, timeouts, rc);
        fclose(report);
        /* Mirror onto ext2 so the report survives reboot for inspection
         * with e2fsck/debugfs on the host. Best effort: /home may be
         * read-only or full in odd images. */
        FILE *dst = fopen("/home/rbtest-report.txt", "w");
        FILE *src = fopen("/tmp/rbtest-report.txt", "r");
        if (dst && src) {
            char b[1024];
            size_t n;
            while ((n = fread(b, 1, sizeof(b), src)) > 0)
                fwrite(b, 1, n, dst);
            fclose(dst);
            fclose(src);
        } else {
            if (dst)
                fclose(dst);
            if (src)
                fclose(src);
        }
    }

    if (getpid() == 1) {
        /* Autotest mode (kernel cmdline `rbtest`): PID1 must not exit —
         * park so the host kills QEMU after seeing DONE. */
        printf("rbtest: init mode, parking (host will power off QEMU)\n");
        fflush(stdout);
        for (;;)
            sleep(1000);
    }
    return rc;
}
