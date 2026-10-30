/* P7.b: pledge(2) positive, negative and exec-inheritance tests.
 *
 * All negative cases run in a forked child: a pledge violation is fatal by
 * design (SIGABRT), so the parent is the only place that can observe it.
 * The exec cases use /usr/bin/pledge-helper, a static no-libc binary whose
 * only path syscall is open("/home/test.txt", O_RDONLY).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TESTFILE "/home/test.txt"
#define HELPER "/usr/bin/pledge-helper"

static void fail(const char *what, int err) {
    printf("PLEDGE-FAIL: %s (errno=%d)\n", what, err);
    _exit(1);
}

/* Fork a child that opens TESTFILE with `flags`. If pledge denies the open,
 * the child dies with SIGABRT. If the open is allowed, the child exits 1 (open
 * succeeded) or 0 (the open failed for a non-pledge reason); either way the
 * parent can tell that no SIGABRT was delivered. */
static pid_t spawn_open(int flags) {
    pid_t pid = fork();
    if (pid < 0)
        fail("fork", errno);
    if (pid == 0) {
        int fd = open(TESTFILE, flags);
        if (fd >= 0) {
            close(fd);
            _exit(1);
        }
        _exit(0);
    }
    return pid;
}

/* Fork a child that execs the no-libc helper. */
static pid_t spawn_helper(void) {
    pid_t pid = fork();
    if (pid < 0)
        fail("fork", errno);
    if (pid == 0) {
        char *argv[] = { (char *)HELPER, NULL };
        char *envp[] = { NULL };
        execve(HELPER, argv, envp);
        _exit(127);
    }
    return pid;
}

static int wait_status(pid_t pid, int *status) {
    if (waitpid(pid, status, 0) != pid)
        return -1;
    return 0;
}

static int expect_sigabrt(pid_t pid, const char *what) {
    int st;
    if (wait_status(pid, &st) != 0) {
        printf("PLEDGE-FAIL: waitpid(%s)\n", what);
        return -1;
    }
    if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGABRT) {
        printf("PLEDGE-FAIL: %s not SIGABRT (status=0x%x)\n", what, st);
        return -1;
    }
    return 0;
}

int main(void) {
    /* Unknown promise names must be rejected without changing the mask. */
    errno = 0;
    if (pledge("bogus", NULL) != -1 || errno != EINVAL)
        fail("unknown promise accepted", errno);
    printf("[ok] unknown promise -> EINVAL\n");

    /* cpath alone must not read an existing file through O_CREAT. This child
     * starts before the parent's first pledge, because cpath cannot be added
     * after rpath has been promised. */
    pid_t cpid = fork();
    if (cpid < 0)
        fail("fork", errno);
    if (cpid == 0) {
        if (pledge("stdio cpath", NULL) != 0)
            _exit(2);
        int cfd = open(TESTFILE, O_RDONLY | O_CREAT, 0600);
        if (cfd >= 0) {
            close(cfd);
            _exit(1);
        }
        _exit(0);
    }
    if (expect_sigabrt(cpid, "cpath read") != 0)
        return 1;
    printf("[ok] cpath alone cannot read existing file\n");

    /* Start with stdio/rpath/proc/exec. exec is kept from the start because
     * pledge can only narrow; later tests must not need to re-add it. */
    if (pledge("stdio rpath proc exec", NULL) != 0)
        fail("initial pledge", errno);
    printf("[ok] pledge(stdio rpath proc exec)\n");

    /* Positive: rpath permits a read-only path open. */
    int fd = open(TESTFILE, O_RDONLY);
    if (fd < 0)
        fail("rpath open denied", errno);
    char byte;
    if (read(fd, &byte, 1) != 1)
        fail("rpath read", errno);
    close(fd);
    printf("[ok] rpath open/read\n");

    /* Negative: no wpath -> O_WRONLY must be fatal. */
    pid_t pid = spawn_open(O_WRONLY);
    if (expect_sigabrt(pid, "wpath open") != 0)
        return 1;
    printf("[ok] wpath denied with SIGABRT\n");

    /* O_RDWR also needs wpath, even though rpath is present. */
    pid = spawn_open(O_RDWR);
    if (expect_sigabrt(pid, "O_RDWR needs wpath") != 0)
        return 1;
    printf("[ok] O_RDWR requires wpath\n");

    /* Monotonicity: asking for wpath after the fact must not grant it. */
    if (pledge("stdio rpath proc exec wpath", NULL) != 0)
        fail("monotonic pledge", errno);
    pid = spawn_open(O_WRONLY);
    if (expect_sigabrt(pid, "wpath after widening attempt") != 0)
        return 1;
    printf("[ok] pledge cannot widen (wpath still denied)\n");

    /* Child narrows itself to stdio: rpath must disappear. */
    pid = fork();
    if (pid < 0)
        fail("fork", errno);
    if (pid == 0) {
        if (pledge("stdio", NULL) != 0)
            _exit(2);
        int cfd = open(TESTFILE, O_RDONLY);
        if (cfd >= 0) {
            close(cfd);
            _exit(1);
        }
        _exit(0);
    }
    if (expect_sigabrt(pid, "child narrowing") != 0)
        return 1;
    printf("[ok] child narrowing drops rpath\n");

    /* Empty promise set: only syscalls that are always allowed (pledge,
     * exit) may run. */
    pid = fork();
    if (pid < 0)
        fail("fork", errno);
    if (pid == 0) {
        if (pledge("", NULL) != 0)
            _exit(2);
        int cfd = open(TESTFILE, O_RDONLY);
        if (cfd >= 0) {
            close(cfd);
            _exit(1);
        }
        _exit(0);
    }
    if (expect_sigabrt(pid, "empty pledge") != 0)
        return 1;
    printf("[ok] empty pledge mask denies open\n");

    /* pledge itself and exit must stay usable even with zero promises. */
    pid = fork();
    if (pid < 0)
        fail("fork", errno);
    if (pid == 0) {
        if (pledge("", NULL) != 0)
            _exit(2);
        if (pledge(NULL, NULL) != 0)
            _exit(3);
        _exit(0);
    }
    int zero_st = 0;
    if (wait_status(pid, &zero_st) != 0 || !WIFEXITED(zero_st) ||
        WEXITSTATUS(zero_st) != 0) {
        printf("PLEDGE-FAIL: pledge/exit under empty mask (status=0x%x)\n",
               zero_st);
        return 1;
    }
    printf("[ok] pledge/exit allowed with empty mask\n");

    /* Bad execpromises must be EINVAL and must not change the state. */
    errno = 0;
    if (pledge("stdio rpath proc exec", "bogus") != -1 || errno != EINVAL)
        fail("bad execpromises accepted", errno);
    printf("[ok] bad execpromises -> EINVAL\n");

    /* Variant C carry-over: execpromises == NULL keeps rpath across exec. */
    if (pledge("stdio rpath proc exec", NULL) != 0)
        fail("carry-over pledge", errno);
    pid = spawn_helper();
    int st = 0;
    if (wait_status(pid, &st) != 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("PLEDGE-FAIL: exec carry-over (status=0x%x)\n", st);
        return 1;
    }
    printf("[ok] exec carries rpath over (variant C)\n");

    /* Explicit execpromises narrows the post-exec mask: no rpath, so the
     * helper's first open is fatal. */
    if (pledge("stdio rpath proc exec", "stdio proc") != 0)
        fail("narrowing execpromises", errno);
    /* pledge(NULL,NULL) is a no-op: it must not widen the pending exec mask
     * back to the active mask (which still contains rpath). */
    if (pledge(NULL, NULL) != 0)
        fail("pledge(NULL,NULL)", errno);
    pid = spawn_helper();
    if (expect_sigabrt(pid, "exec narrowing") != 0)
        return 1;
    printf("[ok] execpromises narrows across exec\n");

    printf("PLEDGE-OK\n");
    return 0;
}
