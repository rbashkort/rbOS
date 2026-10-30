/*
 * sysctltest — P7.c /proc/sys/kernel/{securelevel,loglevel}.
 *
 * Covers:
 *   - the three sysctl files are listed under /proc/sys/kernel;
 *   - loglevel round-trips 0..3, rejects invalid/oversized values, rejects
 *     trailing garbage ("2 foo"), and is not blocked by securelevel;
 *   - non-root cannot write securelevel (0644, EACCES);
 *   - securelevel is monotonic (raise OK, lower EPERM, >2 EINVAL);
 *   - level 1 rejects randomize_va_space=0 but still accepts 2;
 *   - level 2 rejects every randomize_va_space write;
 *   - a write at a non-zero fd offset is EINVAL (VFS advances the offset
 *     after a successful write, so the second write in a row also fails);
 *   - writes longer than 16 bytes are EINVAL before parsing.
 *
 * The test raises securelevel to 2 and cannot lower it: rbtest must run it
 * after every test that relies on randomize_va_space=0, and before crashtest.
 *
 * Prints "SYSCTL-OK" on success, "SYSCTL-FAIL: ..." (exit 1) on failure.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SECURE "/proc/sys/kernel/securelevel"
#define LOGLVL "/proc/sys/kernel/loglevel"
#define ASLR   "/proc/sys/kernel/randomize_va_space"

static int g_fails;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("SYSCTL: check failed: %s (errno=%d)\n", (msg), errno);     \
            g_fails++;                                                         \
            errno = 0;                                                         \
        }                                                                      \
    } while (0)

static int read_num(const char *path, unsigned long *out)
{
    char buf[64];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    int e = errno;
    close(fd);
    errno = e;
    if (n <= 0)
        return -1;
    buf[n] = 0;
    char *end = NULL;
    unsigned long v = strtoul(buf, &end, 10);
    if (end == buf || (*end != 0 && *end != '\n'))
        return -1;
    *out = v;
    return 0;
}

static int write_str(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    size_t len = strlen(s);
    ssize_t n = write(fd, s, len);
    int e = errno;
    close(fd);
    errno = e;
    return (n == (ssize_t)len) ? 0 : -1;
}

static void expect_write_err(const char *path, const char *s, int want,
                             const char *msg)
{
    errno = 0;
    int r = write_str(path, s);
    CHECK(r == -1 && errno == want, msg);
}

static void test_listing(void)
{
    DIR *d = opendir("/proc/sys/kernel");
    CHECK(d != NULL, "opendir /proc/sys/kernel");
    if (!d)
        return;

    int have_secure = 0, have_log = 0, have_aslr = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, "securelevel") == 0)
            have_secure = 1;
        else if (strcmp(de->d_name, "loglevel") == 0)
            have_log = 1;
        else if (strcmp(de->d_name, "randomize_va_space") == 0)
            have_aslr = 1;
    }
    closedir(d);
    CHECK(have_secure, "readdir lists securelevel");
    CHECK(have_log, "readdir lists loglevel");
    CHECK(have_aslr, "readdir keeps randomize_va_space");
}

static void test_loglevel(unsigned long *original)
{
    CHECK(read_num(LOGLVL, original) == 0, "read initial loglevel");

    for (unsigned long v = 0; v <= 3; v++) {
        char s[8];
        snprintf(s, sizeof(s), "%lu\n", v);
        CHECK(write_str(LOGLVL, s) == 0, "write loglevel 0..3");
        unsigned long got = 99;
        CHECK(read_num(LOGLVL, &got) == 0 && got == v,
              "read back loglevel 0..3");
    }

    expect_write_err(LOGLVL, "4\n", EINVAL, "loglevel 4 -> EINVAL");
    expect_write_err(LOGLVL, "-1\n", EINVAL, "loglevel -1 -> EINVAL");
    expect_write_err(LOGLVL, "abc\n", EINVAL, "loglevel abc -> EINVAL");
    expect_write_err(LOGLVL, "2 foo\n", EINVAL,
                     "loglevel trailing garbage -> EINVAL");

    /* A successful write advances the fd offset; a second write without
     * lseek(0) must be rejected (offset != 0). */
    int fd = open(LOGLVL, O_WRONLY);
    CHECK(fd >= 0, "open loglevel for offset test");
    if (fd >= 0) {
        errno = 0;
        CHECK(write(fd, "3\n", 2) == 2, "first loglevel write");
        errno = 0;
        CHECK(write(fd, "3\n", 2) == -1 && errno == EINVAL,
              "second write without lseek -> EINVAL");
        close(fd);
    }

    /* Explicit non-zero offset. */
    fd = open(LOGLVL, O_WRONLY);
    CHECK(fd >= 0, "open loglevel for pwrite-style test");
    if (fd >= 0) {
        errno = 0;
        CHECK(lseek(fd, 1, SEEK_SET) == 1, "lseek loglevel to 1");
        errno = 0;
        CHECK(write(fd, "2\n", 2) == -1 && errno == EINVAL,
              "write at offset 1 -> EINVAL");
        close(fd);
    }

    expect_write_err(LOGLVL, "00000000000000000", EINVAL,
                     "loglevel 17 bytes -> EINVAL");
}

static void test_nonroot_write(void)
{
    pid_t pid = fork();
    CHECK(pid >= 0, "fork non-root writer");
    if (pid == 0) {
        if (setuid(1000) != 0)
            _exit(10);
        errno = 0;
        int fd = open(SECURE, O_WRONLY);
        if (fd >= 0) {
            close(fd);
            _exit(11);
        }
        _exit(errno == EACCES ? 0 : 12);
    }
    if (pid > 0) {
        int st = 0;
        CHECK(waitpid(pid, &st, 0) == pid, "waitpid non-root writer");
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "non-root securelevel write -> EACCES");
    }
}

int main(void)
{
    unsigned long orig_log = 0;
    unsigned long got = 0;

    test_listing();
    test_loglevel(&orig_log);
    test_nonroot_write();

    /* Level 0: ASLR writes are unrestricted. */
    errno = 0;
    CHECK(write_str(ASLR, "0\n") == 0, "level 0: ASLR=0 allowed");
    CHECK(read_num(ASLR, &got) == 0 && got == 0, "level 0: ASLR readback 0");
    CHECK(write_str(ASLR, "2\n") == 0, "level 0: ASLR=2 allowed");
    CHECK(read_num(ASLR, &got) == 0 && got == 2, "level 0: ASLR readback 2");

    /* Securelevel is monotonic. */
    CHECK(read_num(SECURE, &got) == 0 && got == 0,
          "initial securelevel is 0");
    expect_write_err(SECURE, "3\n", EINVAL, "securelevel 3 -> EINVAL");

    CHECK(write_str(SECURE, "1\n") == 0, "raise securelevel to 1");
    CHECK(read_num(SECURE, &got) == 0 && got == 1, "securelevel readback 1");
    expect_write_err(SECURE, "0\n", EPERM, "lower 1 -> 0 -> EPERM");

    /* Level 1: ASLR may stay on, but cannot be turned off. */
    expect_write_err(ASLR, "0\n", EPERM, "level 1: ASLR=0 -> EPERM");
    errno = 0;
    CHECK(write_str(ASLR, "2\n") == 0, "level 1: ASLR=2 allowed");

    CHECK(write_str(SECURE, "2\n") == 0, "raise securelevel to 2");
    CHECK(read_num(SECURE, &got) == 0 && got == 2, "securelevel readback 2");
    expect_write_err(SECURE, "1\n", EPERM, "lower 2 -> 1 -> EPERM");
    expect_write_err(SECURE, "0\n", EPERM, "lower 2 -> 0 -> EPERM");

    /* Level 2: no ASLR writes at all. */
    expect_write_err(ASLR, "0\n", EPERM, "level 2: ASLR=0 -> EPERM");
    expect_write_err(ASLR, "1\n", EPERM, "level 2: ASLR=1 -> EPERM");
    expect_write_err(ASLR, "2\n", EPERM, "level 2: ASLR=2 -> EPERM");

    /* loglevel is diagnostic, not a privilege: level 2 must not block it. */
    errno = 0;
    CHECK(write_str(LOGLVL, "0\n") == 0, "level 2: loglevel=0 allowed");
    CHECK(read_num(LOGLVL, &got) == 0 && got == 0, "level 2: loglevel readback");

    /* Restore the original loglevel; securelevel intentionally stays 2. */
    char restore[8];
    snprintf(restore, sizeof(restore), "%lu\n", orig_log);
    CHECK(write_str(LOGLVL, restore) == 0, "restore original loglevel");

    if (g_fails == 0) {
        printf("SYSCTL-OK\n");
        return 0;
    }
    printf("SYSCTL-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
