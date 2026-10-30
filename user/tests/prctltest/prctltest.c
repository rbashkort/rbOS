/* prctltest — P3.1 prctl(2) MVP regression (plan.md P3.1).
 *
 * Covers the supported subset: PR_GET/SET_DUMPABLE, PR_GET/SET_NAME (with
 * /proc/self/comm reflecting the stored name), PR_SET_VMA(PR_SET_VMA_ANON_NAME)
 * and the EINVAL path for unsupported options. Prints "PRCTL-OK" on success
 * and "PRCTL-FAIL: ..." (exit 1) on the first deviation. Needs no tty.
 */
#include <sys/prctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* PR_SET_VMA postdates mlibc's <sys/prctl.h>; keep the test self-contained. */
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif

static int g_fails;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("PRCTL-FAIL: %s (errno=%d)\n", (msg), errno);                \
            g_fails++;                                                          \
            errno = 0;                                                          \
        }                                                                       \
    } while (0)

/* Read /proc/self/comm, stripping the trailing newline. Returns 0 on success. */
static int read_self_comm(char *buf, size_t cap)
{
    int fd = open("/proc/self/comm", O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, cap - 1);
    close(fd);
    if (r < 0)
        return -1;
    buf[r] = 0;
    size_t l = strlen(buf);
    while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
        buf[--l] = 0;
    return 0;
}

int main(void)
{
    errno = 0;

    /* Linux default is SUID_DUMP_USER (1). */
    CHECK(prctl(PR_GET_DUMPABLE) == 1, "PR_GET_DUMPABLE default == 1");

    CHECK(prctl(PR_SET_DUMPABLE, 0) == 0, "PR_SET_DUMPABLE 0 succeeds");
    CHECK(prctl(PR_GET_DUMPABLE) == 0, "PR_GET_DUMPABLE sees 0");
    CHECK(prctl(PR_SET_DUMPABLE, 1) == 0, "PR_SET_DUMPABLE 1 succeeds");
    CHECK(prctl(PR_GET_DUMPABLE) == 1, "PR_GET_DUMPABLE sees 1");

    /* SUID_DUMP_ROOT (2) is privileged on Linux: unprivileged EINVAL. */
    errno = 0;
    CHECK(prctl(PR_SET_DUMPABLE, 2) == -1 && errno == EINVAL,
          "PR_SET_DUMPABLE 2 -> EINVAL");

    /* PR_SET_NAME / PR_GET_NAME round-trip. */
    const char *name = "prctl-name-abc"; /* 14 bytes */
    CHECK(prctl(PR_SET_NAME, name) == 0, "PR_SET_NAME succeeds");
    char got[16] = {0};
    memset(got, 0xAA, sizeof got);
    CHECK(prctl(PR_GET_NAME, got) == 0, "PR_GET_NAME succeeds");
    CHECK(strcmp(got, name) == 0, "PR_GET_NAME round-trips the name");

    /* /proc/self/comm reflects the stored name (P3.1 procfs wiring). */
    char comm[64];
    CHECK(read_self_comm(comm, sizeof comm) == 0, "read /proc/self/comm");
    CHECK(strcmp(comm, name) == 0, "/proc/self/comm matches stored name");

    /* A too-long name is truncated to 15 bytes, not rejected. */
    CHECK(prctl(PR_SET_NAME, "0123456789abcdefghij") == 0, "PR_SET_NAME long");
    char got2[16] = {0};
    CHECK(prctl(PR_GET_NAME, got2) == 0, "PR_GET_NAME long");
    CHECK(strcmp(got2, "0123456789abcde") == 0, "name truncated to 15 bytes");

    /* Unmapped user pointers are EFAULT, not a kernel fault. */
    errno = 0;
    CHECK(prctl(PR_SET_NAME, (void *)0x1) == -1 && errno == EFAULT,
          "PR_SET_NAME bad pointer -> EFAULT");
    errno = 0;
    CHECK(prctl(PR_GET_NAME, (void *)0x1) == -1 && errno == EFAULT,
          "PR_GET_NAME bad pointer -> EFAULT");

    /* Anonymous VMA naming is accepted (allocators use it); not rendered. */
    CHECK(prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
                (unsigned long)comm & ~0xfffUL, 4096UL,
                (unsigned long)"arena-name") == 0,
          "PR_SET_VMA anon name accepted");

    /* Unknown option: EINVAL, never a silent success. */
    errno = 0;
    CHECK(prctl(0x7fffff00) == -1 && errno == EINVAL,
          "unsupported option -> EINVAL");

    if (g_fails == 0) {
        printf("PRCTL-OK\n");
        return 0;
    }
    printf("PRCTL-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
