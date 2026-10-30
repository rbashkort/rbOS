/* statxtest — P3.2 statx(2) MVP regression (plan.md P3.2).
 *
 * Historically mlibc was built without __MLIBC_LINUX_OPTION, so neither
 * `statx()` nor <abi-bits/statx.h> was available; this file still carries the
 * UAPI layout itself and issues raw statx syscalls (a kernel-level test that
 * stays independent of the libc wrapper linuxtest covers). Covers: BASIC_STATS
 * equal to stat(2) for a regular file and a directory, AT_SYMLINK_NOFOLLOW vs
 * following, the AT_EMPTY_PATH fstat path, honest stx_mask (no BTIME bit),
 * advisory mask, accepted sync/no-automount hints, the error paths (ENOENT for
 * an empty path without AT_EMPTY_PATH and for a missing file, EFAULT, EINVAL
 * for unknown flags, ENOTDIR for a relative path with a real dirfd), and
 * (P3.7/BUG-003) the matching newfstatat empty-path rule.
 *
 * Prints "STATX-OK" on success, "STATX-FAIL: ..." (exit 1) on first failure.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Linux statx(2) UAPI (x86-64), kept in sync with the kernel ---- */
struct statx_timestamp {
    long long tv_sec;
    unsigned int tv_nsec;
    unsigned int __pad;
};

struct statx {
    unsigned int stx_mask;
    unsigned int stx_blksize;
    unsigned long long stx_attributes;
    unsigned int stx_nlink;
    unsigned int stx_uid;
    unsigned int stx_gid;
    unsigned short stx_mode;
    unsigned short __spare0;
    unsigned long long stx_ino;
    unsigned long long stx_size;
    unsigned long long stx_blocks;
    unsigned long long stx_attributes_mask;
    struct statx_timestamp stx_atime;
    struct statx_timestamp stx_btime;
    struct statx_timestamp stx_ctime;
    struct statx_timestamp stx_mtime;
    unsigned int stx_rdev_major;
    unsigned int stx_rdev_minor;
    unsigned int stx_dev_major;
    unsigned int stx_dev_minor;
    unsigned long long stx_mnt_id;
    unsigned int stx_dio_mem_align;
    unsigned int stx_dio_offset_align;
    unsigned long long __spare3[12];
};

#define STATX_TYPE 0x1
#define STATX_BASIC_STATS 0x7ff
#define STATX_BTIME 0x800

#define AT_NO_AUTOMOUNT 0x800
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_EMPTY_PATH 0x1000
#define AT_STATX_FORCE_SYNC 0x2000
#define AT_STATX_DONT_SYNC 0x4000

#define NR_STATX 332

static long statx_sys(int dirfd, const char *path, int flags, unsigned int mask,
                      struct statx *buf)
{
    long ret;
    register long r10 __asm__("r10") = (long)mask;
    register long r8 __asm__("r8") = (long)buf;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"((long)NR_STATX), "D"((long)dirfd), "S"(path),
                       "d"((long)flags), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

static int g_fails;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("STATX-FAIL: %s (errno=%d)\n", (msg), errno);                \
            g_fails++;                                                          \
            errno = 0;                                                          \
        }                                                                       \
    } while (0)

/* Compare a filled statx against a stat(2)/lstat(2) result for the same path. */
static int same_as_stat(const struct statx *sx, const struct stat *st)
{
    int ok = 1;
    ok &= sx->stx_ino == (unsigned long long)st->st_ino;
    ok &= sx->stx_size == (unsigned long long)st->st_size;
    ok &= sx->stx_nlink == (unsigned int)st->st_nlink;
    ok &= sx->stx_uid == (unsigned int)st->st_uid;
    ok &= sx->stx_gid == (unsigned int)st->st_gid;
    ok &= (sx->stx_mode & S_IFMT) == (st->st_mode & S_IFMT);
    ok &= (sx->stx_mode & 07777) == (st->st_mode & 07777);
    ok &= sx->stx_blksize == (unsigned int)st->st_blksize;
    ok &= sx->stx_blocks == (unsigned long long)st->st_blocks;
    ok &= sx->stx_atime.tv_sec == st->st_atim.tv_sec;
    ok &= sx->stx_mtime.tv_sec == st->st_mtim.tv_sec;
    ok &= sx->stx_ctime.tv_sec == st->st_ctim.tv_sec;
    return ok;
}

int main(void)
{
    errno = 0;
    const char *self = "/usr/bin/statxtest";
    const char *dir = "/usr/bin";
    const char *lnk = "/bin"; /* symlink -> usr/bin */

    /* 1. Regular file: statx == stat, stx_mask honest. */
    struct stat st;
    struct statx sx;
    memset(&sx, 0xAA, sizeof sx);
    long r = statx_sys(-100, self, 0, STATX_BASIC_STATS, &sx);
    CHECK(r == 0, "statx(self) succeeds");
    CHECK(stat(self, &st) == 0, "stat(self) succeeds");
    CHECK(sx.stx_mask == STATX_BASIC_STATS, "stx_mask == BASIC_STATS");
    CHECK((sx.stx_mask & STATX_BTIME) == 0, "stx_mask has no BTIME bit");
    CHECK(sx.stx_attributes == 0 && sx.stx_attributes_mask == 0,
          "attributes zeroed");
    CHECK(sx.stx_btime.tv_sec == 0 && sx.stx_mnt_id == 0,
          "btime/mnt_id zeroed");
    CHECK(S_ISREG(st.st_mode) && S_ISREG(sx.stx_mode), "self is a regular file");
    CHECK(same_as_stat(&sx, &st), "statx(self) == stat(self)");

    /* 2. Directory type bit. */
    struct statx dx;
    memset(&dx, 0, sizeof dx);
    CHECK(statx_sys(-100, dir, 0, STATX_BASIC_STATS, &dx) == 0, "statx(dir)");
    CHECK(S_ISDIR(dx.stx_mode), "dir reports S_IFDIR");
    struct stat dst;
    memset(&dst, 0, sizeof dst);
    CHECK(stat(dir, &dst) == 0 && same_as_stat(&dx, &dst),
          "statx(dir) == stat(dir)");

    /* 3. Symlink: follow by default, report the link with NOFOLLOW. */
    struct statx lx;
    memset(&lx, 0, sizeof lx);
    CHECK(statx_sys(-100, lnk, 0, STATX_BASIC_STATS, &lx) == 0,
          "statx(symlink) follows");
    CHECK(S_ISDIR(lx.stx_mode), "followed symlink target is a dir");
    struct statx nlx;
    memset(&nlx, 0, sizeof nlx);
    CHECK(statx_sys(-100, lnk, AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS, &nlx) == 0,
          "statx(symlink, NOFOLLOW)");
    CHECK(S_ISLNK(nlx.stx_mode), "NOFOLLOW reports S_IFLNK");
    struct stat lst;
    memset(&lst, 0, sizeof lst);
    CHECK(lstat(lnk, &lst) == 0 && same_as_stat(&nlx, &lst),
          "statx NOFOLLOW == lstat");

    /* 4. AT_EMPTY_PATH == fstat(fd). */
    int fd = open(self, O_RDONLY);
    CHECK(fd >= 0, "open(self)");
    if (fd >= 0) {
        struct statx fx;
        memset(&fx, 0, sizeof fx);
        CHECK(statx_sys(fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &fx) == 0,
              "statx(fd, EMPTY_PATH)");
        struct stat fst;
        CHECK(fstat(fd, &fst) == 0 && same_as_stat(&fx, &fst),
              "statx EMPTY_PATH == fstat");
        close(fd);
    }

    /* 5. Empty path without AT_EMPTY_PATH -> ENOENT. */
    errno = 0;
    CHECK(statx_sys(-100, "", 0, STATX_BASIC_STATS, &sx) == -ENOENT,
          "empty path without EMPTY_PATH -> ENOENT");

    /* 6. Missing file -> ENOENT. */
    errno = 0;
    CHECK(statx_sys(-100, "/no/such/file", 0, STATX_BASIC_STATS, &sx) == -ENOENT,
          "missing file -> ENOENT");

    /* 7. Unmapped buffer -> EFAULT. */
    errno = 0;
    CHECK(statx_sys(-100, self, 0, STATX_BASIC_STATS, (struct statx *)0x1) == -EFAULT,
          "bad buffer -> EFAULT");

    /* 8. Unknown flag bits -> EINVAL (never a silent success). */
    errno = 0;
    CHECK(statx_sys(-100, self, 0x40000000, STATX_BASIC_STATS, &sx) == -EINVAL,
          "unknown flag -> EINVAL");

    /* 9. Sync/no-automount hints are accepted and change nothing. */
    struct statx hx;
    memset(&hx, 0, sizeof hx);
    CHECK(statx_sys(-100, self, AT_NO_AUTOMOUNT | AT_STATX_FORCE_SYNC,
                    STATX_BASIC_STATS, &hx) == 0,
          "AT_NO_AUTOMOUNT|FORCE_SYNC accepted");
    CHECK(same_as_stat(&hx, &st), "hint result == plain statx");
    memset(&hx, 0, sizeof hx);
    CHECK(statx_sys(-100, self, AT_STATX_DONT_SYNC, STATX_BASIC_STATS, &hx) == 0,
          "AT_STATX_DONT_SYNC accepted");

    /* 10. Requested mask is advisory: mask=0 still yields BASIC_STATS. */
    struct statx mx;
    memset(&mx, 0, sizeof mx);
    CHECK(statx_sys(-100, self, 0, 0, &mx) == 0, "statx(mask=0) succeeds");
    CHECK(mx.stx_mask == STATX_BASIC_STATS, "stx_mask independent of request");

    /* 11. Relative path + real dirfd: inherited *at() policy is ENOTDIR. */
    errno = 0;
    CHECK(statx_sys(0, "statxtest", 0, STATX_BASIC_STATS, &sx) == -ENOTDIR,
          "relative path with real dirfd -> ENOTDIR");

    /* 12. BUG-003: newfstatat(fd, "", 0) must be ENOENT, not a silent fstat;
       with AT_EMPTY_PATH it equals fstat.  statx already enforced this (5.),
       newfstatat did not. */
    fd = open(self, O_RDONLY);
    CHECK(fd >= 0, "open(self) for fstatat empty-path");
    if (fd >= 0) {
        struct stat es;
        memset(&es, 0xAA, sizeof es);
        errno = 0;
        CHECK(fstatat(fd, "", &es, 0) == -1 && errno == ENOENT,
              "fstatat(fd, \"\", 0) -> ENOENT");
        struct stat ef;
        memset(&ef, 0, sizeof ef);
        errno = 0;
        CHECK(fstatat(fd, "", &ef, AT_EMPTY_PATH) == 0,
              "fstatat(fd, \"\", AT_EMPTY_PATH) ok");
        struct stat ff;
        memset(&ff, 0, sizeof ff);
        CHECK(fstat(fd, &ff) == 0
                  && ef.st_ino == ff.st_ino && ef.st_mode == ff.st_mode
                  && ef.st_size == ff.st_size,
              "fstatat EMPTY_PATH == fstat");
        close(fd);
    }

    if (g_fails == 0) {
        printf("STATX-OK\n");
        return 0;
    }
    printf("STATX-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
