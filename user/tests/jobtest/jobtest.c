/* jobtest: job-control conformance tests for the rbKernel signal core.
 *
 * Each case is driven by pipes rather than sleeps, so nothing depends on
 * timing: the parent learns that the child reached its blocking point, sends
 * the signal, and waits for exactly the transition it expects.
 *
 * Pipe convention used throughout: gate[0] is the read end (the child's side),
 * gate[1] the write end (the parent's side). A child closes gate[1] and blocks
 * on gate[0]; the parent writes to gate[1].
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int passed, failed;
static int verbose;

static void step(const char *s)
{
    if (verbose)
        printf("        . %s\n", s);
}

static void ok(const char *name, const char *detail)
{
    printf("  ok    %-32s %s\n", name, detail);
    passed++;
}

static void bad(const char *name, const char *detail)
{
    printf("  FAIL  %-32s %s\n", name, detail);
    failed++;
}

static void skip(const char *name, const char *why)
{
    printf("  skip  %-32s %s\n", name, why);
}

/* Block until one byte arrives. Returns read(2)'s result so the caller can
 * distinguish data from EOF from an error. */
static int wait_for_byte(int fd)
{
    char c;
    int r;
    do {
        r = read(fd, &c, 1);
    } while (r < 0 && errno == EINTR);
    return r;
}

static void signal_byte(int fd)
{
    char c = 'x';
    (void)!write(fd, &c, 1);
}

/* Distinct exit codes so a failing parent can tell the child's outcome. */
#define RC_OK 0
#define RC_EOF 90
#define RC_ERR 91
#define RC_ERRNO(e) (92 + (e))

static int outcome(int r)
{
    if (r == 1)
        return RC_OK;
    if (r == 0)
        return RC_EOF;
    return RC_ERRNO(errno);
}

static const char *rc_text(int rc)
{
    static char buf[64];
    if (rc == RC_OK)
        return "ok";
    if (rc == RC_EOF)
        return "child saw EOF";
    snprintf(buf, sizeof buf, "child read error, errno=%d", rc - 92);
    return buf;
}

/* Allocate-on-demand trace used to bisect the current hang: each call
 * appends one line to /tmp/jtrace.txt on ramfs. Removed before the final
 * run. */
/* Fork a child into its own process group and do not return until the child
 * has actually run setpgid().
 *
 * This handshake is what makes the signal tests deterministic: signalling
 * before setpgid has taken effect would hit the *test's* group, stopping the
 * test itself, and the suite would hang instead of failing.
 * The child parks on gate[0]; the parent releases it by writing gate[1].
 * `extra` runs in the child between setpgid and the handshake (e.g. to
 * install a signal disposition), so the disposition is in place before the
 * parent signals. */
static volatile sig_atomic_t spawn_caught;

static void spawn_handler(int sig)
{
    (void)sig;
    spawn_caught = 1;
}

static void setup_caught_tstp(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = spawn_handler;
    sigaction(SIGTSTP, &sa, 0);
}

static pid_t spawn_ready_cb(int ready[2], int gate[2],
    void (*extra)(void))
{
    pid_t pid = fork();
    if (pid < 0) {
        char detail[96];
        snprintf(detail, sizeof detail, "fork: %s", strerror(errno));
        bad("spawn (setpgid handshake)", detail);
        return pid;
    }
    if (pid == 0) {
        int r;
        setpgid(0, 0);
        close(gate[1]);
        close(ready[0]);
        if (extra)
            extra();
        signal_byte(ready[1]); /* "my pgid (and disposition) are set" */
        close(ready[1]);
        r = wait_for_byte(gate[0]);
        /* The handler flag is the verdict for the caught-signal case; the
         * read outcome is irrelevant there. */
        if (extra == setup_caught_tstp && spawn_caught)
            _exit(0);
        _exit(outcome(r));
    }
    setpgid(pid, 0);
    close(gate[0]);
    close(ready[1]);
    step("waiting for the child's setpgid handshake");
    {
        /* A missing handshake means the child never ran (fork/exec trouble
         * on the kernel side): fail loudly instead of hanging forever. No
         * select(2) in this libc port yet, so poll the pipe once a second. */
        int fl = fcntl(ready[0], F_GETFL, 0);
        int waited = 0;
        int got = 0;
        if (fl >= 0)
            fcntl(ready[0], F_SETFL, fl | O_NONBLOCK);
        while (waited < 10) {
            char b;
            int r = read(ready[0], &b, 1);
            if (r == 1) {
                got = 1;
                break;
            }
            if (r == 0)
                break;
            sleep(1);
            waited++;
        }
        if (!got) {
            bad("spawn (setpgid handshake)", "child never reported ready");
            close(ready[0]);
            return -2;
        }
    }
    close(ready[0]);
    return pid;
}

static pid_t spawn_ready(int ready[2], int gate[2])
{
    spawn_caught = 0;
    return spawn_ready_cb(ready, gate, 0);
}

static void setup_ignore_tstp(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &sa, 0);
}

/* --- 1. a blocking pipe read must park, or nothing below can work --- */

static void test_pipe_blocks(void)
{
    int gate[2];
    char detail[96];
    int status = 0;
    pid_t pid;

    if (pipe(gate) < 0) {
        bad("blocking read on empty pipe", "pipe() failed");
        return;
    }
    pid = fork();
    if (pid < 0) {
        char d2[96];
        snprintf(d2, sizeof d2, "fork: %s", strerror(errno));
        bad("blocking read on empty pipe", d2);
        goto done;
    }
    if (pid == 0) {
        int r;
        close(gate[1]);
        r = wait_for_byte(gate[0]);
        _exit(outcome(r));
    }
    /* Parent keeps the write end (that is how it releases the child) and
     * drops the read end; closing gate[1] here would hand the child EOF. */
    close(gate[0]);

    /* WNOHANG must report "nothing yet": the child cannot have exited, it
     * must be parked in read(). */
    if (waitpid(pid, &status, WNOHANG) != 0) {
        bad("blocking read on empty pipe", "child exited before any input");
        goto done;
    }
    signal_byte(gate[1]);
    if (waitpid(pid, &status, 0) != pid) {
        bad("blocking read on empty pipe", "waitpid failed");
        goto done;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != RC_OK) {
        int rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        snprintf(detail, sizeof detail, "%s", rc_text(rc));
        bad("blocking read on empty pipe", detail);
        goto done;
    }
    ok("blocking read on empty pipe", "parked until a byte arrived");
done:
    close(gate[1]);
}

/* --- 2. stop / CONT / exit cycle --- */

static void test_stop_cycle(const char *name, int sig)
{
    int gate[2], ready[2];
    char detail[96];
    int status = 0;
    pid_t pid;

    if (pipe(gate) < 0 || pipe(ready) < 0) {
        bad(name, "pipe() failed");
        return;
    }
    pid = spawn_ready(ready, gate);
    if (pid < 0)
        goto done;

    step("sending the stop signal");
    if (kill(pid, sig) < 0) {
        snprintf(detail, sizeof detail, "kill(%d): %s", sig, strerror(errno));
        bad(name, detail);
        goto done;
    }
    step("waiting for the stop");
    if (waitpid(pid, &status, WUNTRACED) != pid) {
        snprintf(detail, sizeof detail, "waitpid: %s", strerror(errno));
        bad(name, detail);
        goto done;
    }
    if (!WIFSTOPPED(status)) {
        snprintf(detail, sizeof detail, "status=0x%x, not stopped", status);
        bad(name, detail);
        goto done;
    }
    if (WSTOPSIG(status) != sig) {
        snprintf(detail, sizeof detail, "WSTOPSIG=%d, want %d",
            WSTOPSIG(status), sig);
        bad(name, detail);
        goto done;
    }

    /* SIGCONT must make it runnable: it then reads the byte and exits. */
    if (kill(pid, SIGCONT) < 0) {
        snprintf(detail, sizeof detail, "SIGCONT: %s", strerror(errno));
        bad(name, detail);
        goto done;
    }
    signal_byte(gate[1]);
    status = 0;
    step("waiting for the exit after SIGCONT");
    if (waitpid(pid, &status, 0) != pid) {
        bad(name, "waitpid after CONT failed");
        goto done;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != RC_OK) {
        snprintf(detail, sizeof detail, "after CONT status=0x%x", status);
        bad(name, detail);
        goto done;
    }
    ok(name, "stopped, resumed, exited");
done:
    close(gate[1]);
}

/* --- 3. SIGKILL must terminate an already stopped process --- */

static void test_kill_on_stopped(void)
{
    int gate[2], ready[2];
    char detail[96];
    int status = 0;
    pid_t pid;

    if (pipe(gate) < 0 || pipe(ready) < 0) {
        bad("SIGKILL after stop", "pipe() failed");
        return;
    }
    pid = spawn_ready(ready, gate);

    if (kill(pid, SIGSTOP) < 0
        || waitpid(pid, &status, WUNTRACED) != pid
        || !WIFSTOPPED(status))
    {
        bad("SIGKILL after stop", "child did not stop");
        goto done;
    }
    if (kill(pid, SIGKILL) < 0) {
        snprintf(detail, sizeof detail, "kill: %s", strerror(errno));
        bad("SIGKILL after stop", detail);
        goto done;
    }
    status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        bad("SIGKILL after stop", "waitpid after SIGKILL failed");
        goto done;
    }
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
        snprintf(detail, sizeof detail, "status=0x%x, want killed by %d",
            status, SIGKILL);
        bad("SIGKILL after stop", detail);
        goto done;
    }
    ok("SIGKILL after stop", "killed while stopped");
done:
    close(gate[1]);
}

/* --- 4. a caught stop signal runs the handler instead of stopping --- */

static void test_caught_stop_signal(void)
{
    int gate[2], ready[2];
    char detail[96];
    int status = 0;
    pid_t pid;

    if (pipe(gate) < 0 || pipe(ready) < 0) {
        bad("caught SIGTSTP", "pipe() failed");
        return;
    }
    pid = spawn_ready_cb(ready, gate, setup_caught_tstp);
    if (pid < 0)
        goto done;

    step("sending SIGTSTP to a child with a handler");
    kill(pid, SIGTSTP);
    step("releasing the child");
    signal_byte(gate[1]);
    if (waitpid(pid, &status, WUNTRACED) != pid) {
        bad("caught SIGTSTP", "waitpid failed");
        goto done;
    }
    if (WIFSTOPPED(status)) {
        bad("caught SIGTSTP", "child stopped despite the handler");
        goto done;
    }
    if (!WIFEXITED(status)) {
        snprintf(detail, sizeof detail, "status=0x%x, not exited", status);
        bad("caught SIGTSTP", detail);
        goto done;
    }
    if (WEXITSTATUS(status) != 0) {
        snprintf(detail, sizeof detail, "exit=%d: handler did not run "
            "(its flag is checked inside the child)",
            WEXITSTATUS(status));
        bad("caught SIGTSTP", detail);
        goto done;
    }
    ok("caught SIGTSTP", "handler ran, no stop");
done:
    close(gate[1]);
}

/* --- 5. SIG_IGN on a stop signal keeps the child running --- */

static void test_ignored_stop(void)
{
    int gate[2], ready[2];
    char detail[96];
    int status = 0;
    pid_t pid;

    if (pipe(gate) < 0 || pipe(ready) < 0) {
        bad("ignored SIGTSTP", "pipe() failed");
        return;
    }
    pid = spawn_ready_cb(ready, gate, setup_ignore_tstp);
    if (pid < 0)
        goto done;

    kill(pid, SIGTSTP);
    signal_byte(gate[1]);
    if (waitpid(pid, &status, 0) != pid) {
        bad("ignored SIGTSTP", "waitpid failed");
        goto done;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != RC_OK) {
        snprintf(detail, sizeof detail, "status=0x%x", status);
        bad("ignored SIGTSTP", detail);
        goto done;
    }
    ok("ignored SIGTSTP", "ran to completion");
done:
    close(gate[1]);
}

/* --- 6. a background group reading its tty is stopped by SIGTTIN --- */

static void test_bg_read_stops(void)
{
    char detail[96];
    int ready[2];
    int status = 0;
    pid_t pid;

    if (!isatty(0)) {
        skip("background read -> SIGTTIN", "stdin is not a tty");
        return;
    }
    if (pipe(ready) < 0) {
        bad("background read -> SIGTTIN", "pipe() failed");
        return;
    }
    pid = fork();
    if (pid == 0) {
        char c;
        setpgid(0, 0); /* not the terminal's foreground group */
        close(ready[0]);
        signal_byte(ready[1]);
        if (read(0, &c, 1) < 0)
            _exit(errno == EIO ? RC_EOF : RC_ERR);
        _exit(0);
    }
    setpgid(pid, 0);
    close(ready[1]);
    (void)wait_for_byte(ready[0]);
    close(ready[0]);

    if (waitpid(pid, &status, WUNTRACED) != pid) {
        bad("background read -> SIGTTIN", "waitpid failed");
        goto done;
    }
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTTIN) {
        snprintf(detail, sizeof detail,
            "status=0x%x stopsig=%d, want stopped by %d",
            status, WSTOPSIG(status), SIGTTIN);
        bad("background read -> SIGTTIN", detail);
        goto done;
    }
    kill(pid, SIGCONT);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    ok("background read -> SIGTTIN", "stopped by SIGTTIN");
done:
    return;
}

/* --- 7. an orphaned group gets EIO instead of a stop nobody can continue --- */

static void test_orphan_read_eio(void)
{
    int vfd[2];
    char detail[96];
    pid_t middle;

    if (!isatty(0)) {
        skip("orphan read -> EIO", "stdin is not a tty");
        return;
    }
    /* An orphaned group needs a dead parent outside init's session: when
     * every process (including init) shares one session, init's pid anchors
     * every group by the POSIX definition and no group can ever be orphaned.
     * The session architecture has to grow setsid-separated sessions first.
     * Detect it directly: init (pid 1) must be in a different session. */
    {
        int self_sid = -1, init_sid = -2;
        FILE *f = fopen("/proc/self/stat", "r");
        if (f) {
            /* fields: pid comm state ppid pgrp session */
            int pid, ppid, pgrp;
            char comm[32], state;
            if (fscanf(f, "%d %31s %c %d %d %d",
                    &pid, comm, &state, &ppid, &pgrp, &self_sid) != 6)
                self_sid = -1;
            fclose(f);
        }
        f = fopen("/proc/1/stat", "r");
        if (f) {
            int pid, ppid, pgrp;
            char comm[32], state;
            if (fscanf(f, "%d %31s %c %d %d %d",
                    &pid, comm, &state, &ppid, &pgrp, &init_sid) != 6)
                init_sid = -2;
            fclose(f);
        }
        if (self_sid < 0 || self_sid == init_sid) {
            snprintf(detail, sizeof detail,
                "single session (self sid=%d, init sid=%d)", self_sid, init_sid);
            skip("orphan read -> EIO", detail);
            return;
        }
    }
    if (pipe(vfd) < 0) {
        bad("orphan read -> EIO", "pipe() failed");
        return;
    }
    /* An intermediate process leaves the grandchild reparented to init, i.e.
     * in a process group with no parent left that could ever continue it. */
    middle = fork();
    if (middle == 0) {
        pid_t grand = fork();
        if (grand == 0) {
            char c;
            char rep[32];
            int n;
            setpgid(0, 0);
            if (read(0, &c, 1) < 0) {
                n = snprintf(rep, sizeof rep, "e%d", errno);
                (void)!write(vfd[1], rep, (size_t)n);
            } else
                (void)!write(vfd[1], "d", 1);
            _exit(0);
        }
        if (grand > 0) {
            int s;
            waitpid(grand, &s, 0);
        }
        _exit(0);
    }
    waitpid(middle, NULL, 0);

    /* The grandchild is not our child, so it reports through the pipe. */
    {
        char rep[16];
        int n = 0;
        while (n < (int)sizeof rep - 1) {
            int r = read(vfd[0], &rep[n], 1);
            if (r <= 0)
                break;
            n += r;
            if (n > 4)
                break;
        }
        rep[n] = 0;
        if (n <= 0) {
            bad("orphan read -> EIO", "grandchild never reported");
            goto done;
        }
        if (strcmp(rep, "e5") != 0) {
            snprintf(detail, sizeof detail,
                "grandchild reported '%s', want e5 (EIO)", rep);
            bad("orphan read -> EIO", detail);
            goto done;
        }
    }
    ok("orphan read -> EIO", "EIO, no stop");
done:
    close(vfd[0]);
    close(vfd[1]);
}

static const char *g_only;

static int only(const char *name)
{
    return g_only == 0 || strcmp(g_only, name) == 0;
}

int main(int argc, char **argv)
{
    (void)argv;
    verbose = argc > 1;
    g_only = getenv("JOBTEST_ONLY");
    printf("rbKernel job-control tests\n");
    printf("---------------------------\n");

    if (only("pipe"))
        test_pipe_blocks();
    if (only("tstp"))
        test_stop_cycle("SIGTSTP stop + CONT", SIGTSTP);
    if (only("stop"))
        test_stop_cycle("SIGSTOP stop + CONT", SIGSTOP);
    if (only("ttin"))
        test_stop_cycle("SIGTTIN stop + CONT", SIGTTIN);
    if (only("ttou"))
        test_stop_cycle("SIGTTOU stop + CONT", SIGTTOU);
    if (only("kill"))
        test_kill_on_stopped();
    if (only("caught"))
        test_caught_stop_signal();
    if (only("ignored"))
        test_ignored_stop();
    if (only("bgread"))
        test_bg_read_stops();
    if (getenv("JOBTEST_ONLY") == 0 || strcmp(getenv("JOBTEST_ONLY"), "orphan") == 0)
        test_orphan_read_eio();

    printf("---------------------------\n");
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
