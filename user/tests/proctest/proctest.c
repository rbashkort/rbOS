/* proctest — P3.7 procfs regression for BUG-001 (/proc/<pid>/cmdline serves
 * the raw NUL-separated argv, not a space-joined string) and BUG-002 (comm is
 * served verbatim — tabs and other bytes survive; only /proc/<pid>/stat maps
 * '(' / ')' to '_' so its field delimiter stays unambiguous).
 *
 * The argv check re-execs the test with several arguments first, so the
 * NUL-vs-space distinction is observable (a single-argument re-exec would make
 * the check vacuous; if execv fails the run is merely weaker, not wrong).
 *
 * Prints "PROC-OK" on success, "PROC-FAIL: <msg>" (exit 1) on a failed check.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

static int g_fails;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("PROC-FAIL: %s (errno=%d)\n", (msg), errno);                 \
            g_fails++;                                                          \
            errno = 0;                                                          \
        }                                                                       \
    } while (0)

static ssize_t slurp(const char *path, unsigned char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, cap - 1);
    if (n >= 0)
        buf[n] = 0;
    close(fd);
    return n;
}

int main(int argc, char **argv)
{
    /* BUG-001: obtain a multi-argument cmdline by re-execing ourselves. */
    if (argc < 3) {
        char *child[] = { "/usr/bin/proctest", "alpha", "beta gamma", NULL };
        execv("/usr/bin/proctest", child);
        /* execv failed: continue with the argv we already have. */
    }

    unsigned char buf[1024];
    ssize_t n = slurp("/proc/self/cmdline", buf, sizeof buf);
    CHECK(n > 0, "cmdline readable");
    if (n > 0) {
        /* NUL-separated and NUL-terminated: exactly one NUL per argv entry
           (a space separator would yield a single NUL and break this). */
        int nuls = 0;
        for (ssize_t i = 0; i < n; i++)
            if (buf[i] == 0)
                nuls++;
        CHECK(nuls == argc, "cmdline has one NUL per argv entry");
        CHECK(memcmp(buf, argv[0], strlen(argv[0])) == 0,
              "cmdline argv[0] matches");
        if (argc >= 3) {
            const unsigned char *a1 = buf + strlen(argv[0]) + 1;
            CHECK(strcmp((const char *)a1, argv[1]) == 0,
                  "cmdline argv[1] == alpha");
            const unsigned char *a2 = a1 + strlen(argv[1]) + 1;
            CHECK(strcmp((const char *)a2, argv[2]) == 0,
                  "cmdline argv[2] == 'beta gamma' (interior space kept)");
        }
    }

    /* BUG-002: a tab must survive in comm and in status' Name: line, and
       PR_GET_NAME must agree (the old renderer dropped non-graphic bytes). */
    const char raw[] = "a\tb";
    CHECK(prctl(PR_SET_NAME, raw) == 0, "prctl(PR_SET_NAME, \"a\\tb\")");
    memset(buf, 0, sizeof buf);
    n = slurp("/proc/self/comm", buf, sizeof buf);
    CHECK(n == 4 && buf[0] == 'a' && buf[1] == '\t' && buf[2] == 'b'
              && buf[3] == '\n',
          "comm keeps the raw tab");
    char got[16];
    memset(got, 0, sizeof got);
    CHECK(prctl(PR_GET_NAME, got) == 0 && strcmp(got, raw) == 0,
          "PR_GET_NAME == raw name");
    memset(buf, 0, sizeof buf);
    n = slurp("/proc/self/status", buf, sizeof buf);
    CHECK(n > 0 && strstr((const char *)buf, "Name:\ta\tb\n") != NULL,
          "status Name: is raw");

    /* BUG-002: parentheses stay raw in comm, but /proc/<pid>/stat keeps its
       "(comm)" field parseable by mapping them to '_'. */
    CHECK(prctl(PR_SET_NAME, "x(y)z") == 0, "prctl(PR_SET_NAME, \"x(y)z\")");
    memset(buf, 0, sizeof buf);
    n = slurp("/proc/self/comm", buf, sizeof buf);
    CHECK(n == 6 && memcmp(buf, "x(y)z\n", 6) == 0, "comm keeps parens (raw)");
    memset(buf, 0, sizeof buf);
    n = slurp("/proc/self/stat", buf, sizeof buf);
    if (n > 0) {
        char *lp = strchr((char *)buf, '(');
        char *rp = lp ? strchr(lp + 1, ')') : NULL;
        CHECK(lp && rp && (rp - lp - 1) == 5
                  && memcmp(lp + 1, "x_y_z", 5) == 0,
              "stat (comm) maps parens to '_'");
    } else {
        CHECK(0, "stat readable");
    }

    if (g_fails == 0) {
        printf("PROC-OK\n");
        return 0;
    }
    printf("PROC-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
