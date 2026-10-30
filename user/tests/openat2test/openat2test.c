/* openat2test — P3.3 openat2(437) MVP regression (plan.md P3.3).
 *
 * mlibc here is built without __MLIBC_LINUX_OPTION, so neither `openat2()`
 * nor <linux/openat2.h> is available; this file carries the UAPI layout
 * itself and issues raw syscall 437. Covers: openat2 == openat for reads,
 * O_CREAT|O_WRONLY|O_TRUNC create/write/read-back, O_CLOEXEC honoured
 * (fcntl(F_GETFD)), the size/version protocol (EINVAL < VER0, E2BIG on a
 * non-zero tail, zero-padded larger struct accepted), resolve-flag honesty
 * (NO_MAGICLINKS|CACHED accepted; BENEATH/IN_ROOT/NO_XDEV/NO_SYMLINKS/unknown
 * rejected with EINVAL), mode without O_CREAT -> EINVAL, and the error paths
 * (ENOENT, EFAULT, ENOTDIR for a relative path with a real dirfd).
 *
 * Prints "OPENAT2-OK" on success, "OPENAT2-FAIL: ..." (exit 1) on first failure.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ---- Linux openat2(2) UAPI (x86-64), kept in sync with the kernel ---- */
struct open_how {
    unsigned long long flags;
    unsigned long long mode;
    unsigned long long resolve;
};

#define RESOLVE_NO_XDEV 0x01
#define RESOLVE_NO_MAGICLINKS 0x02
#define RESOLVE_NO_SYMLINKS 0x04
#define RESOLVE_BENEATH 0x08
#define RESOLVE_IN_ROOT 0x10
#define RESOLVE_CACHED 0x20

#define NR_OPENAT2 437

static long openat2_sys(int dirfd, const char *path, const void *how,
                        unsigned long size)
{
    long ret;
    register long r10 __asm__("r10") = (long)size;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"((long)NR_OPENAT2), "D"((long)dirfd), "S"(path),
                       "d"((long)how), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static int g_fails;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("OPENAT2-FAIL: %s (errno=%d)\n", (msg), errno);              \
            g_fails++;                                                          \
            errno = 0;                                                          \
        }                                                                       \
    } while (0)

int main(void)
{
    errno = 0;
    const char *self = "/usr/bin/openat2test";
    const char *tmpfile = "/tmp/openat2test.txt";
    struct open_how how;

    /* 1. Plain read: openat2 == openat, same bytes. */
    memset(&how, 0, sizeof how);
    how.flags = O_RDONLY;
    long fd2 = openat2_sys(-100, self, &how, sizeof how);
    CHECK(fd2 >= 0, "openat2(O_RDONLY) succeeds");
    if (fd2 >= 0) {
        char a[64], b[64];
        int fda = open(self, O_RDONLY);
        CHECK(fda >= 0, "open(self) succeeds");
        ssize_t na = fda >= 0 ? read(fda, a, sizeof a) : -1;
        ssize_t nb = read((int)fd2, b, sizeof b);
        CHECK(na > 0 && na == nb && memcmp(a, b, (size_t)na) == 0,
              "openat2 read == open read");
        if (fda >= 0)
            close(fda);
        close((int)fd2);
    }

    /* 2. O_CREAT|O_WRONLY|O_TRUNC creates, writes and reads back. */
    unlink(tmpfile);
    memset(&how, 0, sizeof how);
    how.flags = O_CREAT | O_WRONLY | O_TRUNC;
    how.mode = 0644;
    long cfd = openat2_sys(-100, tmpfile, &how, sizeof how);
    CHECK(cfd >= 0, "openat2(O_CREAT|O_WRONLY|O_TRUNC) succeeds");
    if (cfd >= 0) {
        CHECK(write((int)cfd, "hello\n", 6) == 6, "write via openat2 fd");
        close((int)cfd);
        int rd = open(tmpfile, O_RDONLY);
        CHECK(rd >= 0, "reopen created file");
        if (rd >= 0) {
            char buf[8];
            memset(buf, 0, sizeof buf);
            CHECK(read(rd, buf, sizeof buf) == 6 && memcmp(buf, "hello\n", 6) == 0,
                  "created content round-trips");
            close(rd);
        }
        unlink(tmpfile);
    }

    /* 3. O_CLOEXEC is honoured (fcntl(F_GETFD) reports FD_CLOEXEC), for both
     *    openat2 and plain open (regression: kernel vfs::O_CLOEXEC was off by
     *    an octal digit, so this silently never matched). */
    memset(&how, 0, sizeof how);
    how.flags = O_RDONLY | O_CLOEXEC;
    long efd = openat2_sys(-100, self, &how, sizeof how);
    CHECK(efd >= 0, "openat2(O_CLOEXEC) succeeds");
    if (efd >= 0) {
        CHECK((fcntl((int)efd, F_GETFD) & FD_CLOEXEC) != 0,
              "openat2 O_CLOEXEC -> FD_CLOEXEC set");
        close((int)efd);
    }
    int pfd = open(self, O_RDONLY | O_CLOEXEC);
    CHECK(pfd >= 0, "open(O_CLOEXEC) succeeds");
    if (pfd >= 0) {
        CHECK((fcntl(pfd, F_GETFD) & FD_CLOEXEC) != 0,
              "open O_CLOEXEC -> FD_CLOEXEC set");
        close(pfd);
    }

    /* 4. Resolve flags we can honour: NO_MAGICLINKS|CACHED accepted. */
    memset(&how, 0, sizeof how);
    how.flags = O_RDONLY;
    how.resolve = RESOLVE_NO_MAGICLINKS | RESOLVE_CACHED;
    long r = openat2_sys(-100, self, &how, sizeof how);
    CHECK(r >= 0, "resolve NO_MAGICLINKS|CACHED accepted");
    if (r >= 0)
        close((int)r);

    /* 5. Resolve flags we cannot enforce are refused, not ignored. */
    static const unsigned long unsupported[] = {
        RESOLVE_NO_XDEV, RESOLVE_NO_SYMLINKS, RESOLVE_BENEATH, RESOLVE_IN_ROOT,
    };
    for (unsigned i = 0; i < sizeof unsupported / sizeof unsupported[0]; i++) {
        memset(&how, 0, sizeof how);
        how.flags = O_RDONLY;
        how.resolve = unsupported[i];
        errno = 0;
        CHECK(openat2_sys(-100, self, &how, sizeof how) == -EINVAL,
              "unsupported resolve bit -> EINVAL");
    }
    memset(&how, 0, sizeof how);
    how.flags = O_RDONLY;
    how.resolve = 0x40; /* beyond RESOLVE_VALID */
    errno = 0;
    CHECK(openat2_sys(-100, self, &how, sizeof how) == -EINVAL,
          "unknown resolve bit -> EINVAL");

    /* 6. size/version protocol. */
    memset(&how, 0, sizeof how);
    how.flags = O_RDONLY;
    errno = 0;
    CHECK(openat2_sys(-100, self, &how, 0) == -EINVAL, "size 0 -> EINVAL");
    errno = 0;
    CHECK(openat2_sys(-100, self, &how, 23) == -EINVAL, "size 23 -> EINVAL");
    r = openat2_sys(-100, self, &how, 24);
    CHECK(r >= 0, "size 24 accepted");
    if (r >= 0)
        close((int)r);

    /* Zero-padded larger struct is accepted (forward-compat). */
    unsigned char big[64];
    memset(big, 0, sizeof big);
    ((struct open_how *)big)->flags = O_RDONLY;
    r = openat2_sys(-100, self, big, sizeof big);
    CHECK(r >= 0, "size 64 zero-padded accepted");
    if (r >= 0)
        close((int)r);

    /* A non-zero byte past VER0 must be E2BIG. */
    big[24] = 0xAA;
    errno = 0;
    CHECK(openat2_sys(-100, self, big, sizeof big) == -E2BIG,
          "size 64 non-zero tail -> E2BIG");

    /* 7. mode without O_CREAT -> EINVAL. */
    memset(&how, 0, sizeof how);
    how.flags = O_RDONLY;
    how.mode = 0644;
    errno = 0;
    CHECK(openat2_sys(-100, self, &how, sizeof how) == -EINVAL,
          "mode without O_CREAT -> EINVAL");

    /* 8. Missing file -> ENOENT. */
    memset(&how, 0, sizeof how);
    how.flags = O_RDONLY;
    errno = 0;
    CHECK(openat2_sys(-100, "/no/such/file", &how, sizeof how) == -ENOENT,
          "missing file -> ENOENT");

    /* 9. Unmapped how pointer -> EFAULT. */
    errno = 0;
    CHECK(openat2_sys(-100, self, (const void *)0x1, sizeof how) == -EFAULT,
          "bad how pointer -> EFAULT");

    /* 10. Relative path + real dirfd: inherited *at() policy is ENOTDIR. */
    int dfd = open("/usr/bin", O_RDONLY);
    CHECK(dfd >= 0, "open(/usr/bin) for dirfd");
    if (dfd >= 0) {
        memset(&how, 0, sizeof how);
        how.flags = O_RDONLY;
        errno = 0;
        CHECK(openat2_sys(dfd, "openat2test", &how, sizeof how) == -ENOTDIR,
              "relative path with real dirfd -> ENOTDIR");
        close(dfd);
    }

    if (g_fails == 0) {
        printf("OPENAT2-OK\n");
        return 0;
    }
    printf("OPENAT2-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
