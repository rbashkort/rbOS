/* linuxtest — P3.6 mlibc linux-option regression (plan.md P3.6).
 *
 * Before P3.6 mlibc was built with __MLIBC_LINUX_OPTION=0, so the Linux API
 * set was absent and P3.1/P3.2 provided prctl()/statx() by hand (a directly
 * defined symbol / raw syscalls, see bugs.md BUG-004). This test exercises the
 * wrappers mlibc's linux option now ships and the sysdeps that back them,
 * through the public headers only:
 *
 *   - prctl()      <sys/prctl.h>     -> options/linux Prctl sysdep
 *   - statx()      <sys/stat.h>      -> options/linux Statx sysdep (real
 *                                       statx, not the stat() fallback)
 *   - getentropy() <unistd.h> and getrandom() <sys/random.h> -> GetEntropy
 *   - if_nameindex() <net/if.h> returning ENOSYS: net-if must still compile
 *     under the option even though rbOS has no rtnetlink stack (BUG-008).
 *
 * Prints "LINUX-OK" on success, "LINUX-FAIL: ..." (exit 1) on first failure.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <net/if.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_fails;

/* Raw syscalls glibc/mlibc wrappers don't issue: clock_nanosleep(230) is
 * emulated in libc via nanosleep, so nr 230 is only reachable raw. */
static long sys2(long nr, unsigned long a1, unsigned long a2) {
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static long sys4(long nr, unsigned long a1, unsigned long a2,
                 unsigned long a3, unsigned long a4) {
    long ret;
    register long r10 __asm__("r10") = (long)a4;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory");
    return ret;
}

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("LINUX-FAIL: %s (errno=%d)\n", (msg), errno);               \
            g_fails++;                                                         \
            errno = 0;                                                         \
        }                                                                      \
    } while (0)

int main(void)
{
    errno = 0;

    /* ---- prctl(2) via the mlibc wrapper ------------------------------- */
    char name[16];
    memset(name, 0, sizeof(name));
    CHECK(prctl(PR_GET_NAME, name) == 0, "prctl(PR_GET_NAME) succeeds");
    CHECK(prctl(PR_SET_NAME, "linuxtest") == 0, "prctl(PR_SET_NAME) succeeds");
    memset(name, 0, sizeof(name));
    CHECK(prctl(PR_GET_NAME, name) == 0, "prctl(PR_GET_NAME) round-trip");
    CHECK(strcmp(name, "linuxtest") == 0, "PR_GET_NAME reflects PR_SET_NAME");

    errno = 0;
    CHECK(prctl(-1) == -1 && errno == EINVAL, "prctl(unknown op) -> EINVAL");

    /* ---- statx(2) via the mlibc wrapper ------------------------------- */
    const char *self = "/usr/bin/linuxtest";
    struct statx sx;
    struct stat st;
    memset(&sx, 0, sizeof(sx));
    int r = statx(AT_FDCWD, self, 0, STATX_BASIC_STATS, &sx);
    CHECK(r == 0, "statx() wrapper succeeds on own binary");
    CHECK(stat(self, &st) == 0, "stat() succeeds on own binary");
    CHECK(sx.stx_size == (unsigned long long)st.st_size, "statx size == stat size");
    CHECK(sx.stx_ino == st.st_ino, "statx ino == stat ino");
    CHECK(sx.stx_mode == (unsigned short)st.st_mode, "statx mode == stat mode");
    CHECK((sx.stx_mask & STATX_BASIC_STATS) != 0, "statx reports BASIC_STATS");

    errno = 0;
    CHECK(statx(AT_FDCWD, "/no/such/file", 0, STATX_BASIC_STATS, &sx) == -1
              && errno == ENOENT,
          "statx(missing) -> ENOENT");

    /* ---- getentropy(3) / getrandom(2) --------------------------------- */
    unsigned char e1[16], e2[16];
    memset(e1, 0, sizeof(e1));
    memset(e2, 0, sizeof(e2));
    CHECK(getentropy(e1, sizeof(e1)) == 0, "getentropy(16) succeeds");
    CHECK(getentropy(e2, sizeof(e2)) == 0, "getentropy(16) succeeds (2nd)");
    CHECK(memcmp(e1, e2, sizeof(e1)) != 0, "two getentropy draws differ");

    errno = 0;
    CHECK(getentropy(e1, 257) == -1 && errno == EINVAL,
          "getentropy(>256) -> EINVAL");

    memset(e1, 0, sizeof(e1));
    ssize_t n = getrandom(e1, sizeof(e1), 0);
    CHECK(n == (ssize_t)sizeof(e1), "getrandom(16, 0) -> 16");

    errno = 0;
    CHECK(getrandom(e1, sizeof(e1), ~0u) == -1 && errno == EINVAL,
          "getrandom(bad flags) -> EINVAL");

    /* ---- net-if under the linux option (no rtnetlink) ----------------- */
    errno = 0;
    CHECK(if_nametoindex("lo") == 0 && errno == ENOSYS,
          "if_nametoindex() -> ENOSYS (no rtnetlink)");
    errno = 0;
    static char ifname[IF_NAMESIZE];
    CHECK(if_indextoname(1, ifname) == NULL && errno == ENOSYS,
          "if_indextoname() -> ENOSYS (no rtnetlink)");
    errno = 0;
    struct if_nameindex *ni = if_nameindex();
    CHECK(ni == NULL && errno == ENOSYS, "if_nameindex() -> ENOSYS (no rtnetlink)");

    /* ---- statfs(2)/fstatfs(2): procps-ng `top` prerequisites ---------- */
    errno = 0;
    struct statfs sf;
    memset(&sf, 0, sizeof(sf));
    CHECK(statfs("/proc", &sf) == 0, "statfs(/proc) succeeds");
    CHECK(sf.f_type == 0x9fa0, "statfs(/proc) f_type == PROC_SUPER_MAGIC");
    errno = 0;
    memset(&sf, 0, sizeof(sf));
    CHECK(statfs("/", &sf) == 0, "statfs(/) succeeds");
    CHECK(sf.f_type == 0xEF53, "statfs(/) f_type == EXT2_SUPER_MAGIC");
    errno = 0;
    CHECK(statfs("/no/such/file", &sf) == -1 && errno == ENOENT,
          "statfs(missing) -> ENOENT");
    int sfd = open("/proc/stat", O_RDONLY);
    CHECK(sfd >= 0, "open(/proc/stat) succeeds");
    if (sfd >= 0) {
        errno = 0;
        memset(&sf, 0, sizeof(sf));
        CHECK(fstatfs(sfd, &sf) == 0, "fstatfs(/proc/stat) succeeds");
        CHECK(sf.f_type == 0x9fa0, "fstatfs(/proc/stat) f_type == PROC_SUPER_MAGIC");
        close(sfd);
    }
    errno = 0;
    CHECK(fstatfs(-1, &sf) == -1 && errno == EBADF, "fstatfs(badfd) -> EBADF");

    /* ---- /proc files `top` parses ------------------------------------ */
    char pbuf[4096];
    int fd = open("/proc/mounts", O_RDONLY);
    CHECK(fd >= 0, "open(/proc/mounts) succeeds");
    if (fd >= 0) {
        ssize_t m = read(fd, pbuf, sizeof(pbuf) - 1);
        close(fd);
        CHECK(m > 0, "/proc/mounts non-empty");
        if (m > 0) {
            pbuf[m] = '\0';
            CHECK(strstr(pbuf, "proc /proc proc") != NULL,
                  "/proc/mounts lists proc /proc proc");
        }
    }
    fd = open("/proc/self/mountinfo", O_RDONLY);
    CHECK(fd >= 0, "open(/proc/self/mountinfo) succeeds");
    if (fd >= 0) {
        ssize_t m = read(fd, pbuf, sizeof(pbuf) - 1);
        close(fd);
        CHECK(m > 0, "/proc/self/mountinfo non-empty");
        if (m > 0) {
            pbuf[m] = '\0';
            CHECK(strstr(pbuf, "proc") != NULL, "/proc/self/mountinfo mentions proc");
        }
    }
    fd = open("/proc/stat", O_RDONLY);
    CHECK(fd >= 0, "open(/proc/stat) succeeds");
    if (fd >= 0) {
        ssize_t m = read(fd, pbuf, sizeof(pbuf) - 1);
        close(fd);
        CHECK(m > 0, "/proc/stat non-empty");
        if (m > 0) {
            pbuf[m] = '\0';
            CHECK(strstr(pbuf, "cpu ") != NULL, "/proc/stat has cpu line");
            CHECK(strstr(pbuf, "btime ") != NULL, "/proc/stat has btime");
            CHECK(strstr(pbuf, "processes ") != NULL, "/proc/stat has processes");
        }
    }
    fd = open("/proc/uptime", O_RDONLY);
    CHECK(fd >= 0, "open(/proc/uptime) succeeds");
    if (fd >= 0) {
        ssize_t m = read(fd, pbuf, sizeof(pbuf) - 1);
        close(fd);
        CHECK(m > 0, "/proc/uptime non-empty");
        if (m > 0) {
            pbuf[m] = '\0';
            double a = -1, b = -1;
            CHECK(sscanf(pbuf, "%lf %lf", &a, &b) == 2, "/proc/uptime has two floats");
            CHECK(a >= 0 && b >= 0, "/proc/uptime values non-negative");
        }
    }
    fd = open("/proc/meminfo", O_RDONLY);
    CHECK(fd >= 0, "open(/proc/meminfo) succeeds");
    if (fd >= 0) {
        ssize_t m = read(fd, pbuf, sizeof(pbuf) - 1);
        close(fd);
        CHECK(m > 0, "/proc/meminfo non-empty");
        if (m > 0) {
            pbuf[m] = '\0';
            CHECK(strstr(pbuf, "MemTotal:") != NULL, "/proc/meminfo has MemTotal");
            CHECK(strstr(pbuf, "MemFree:") != NULL, "/proc/meminfo has MemFree");
            CHECK(strstr(pbuf, "Buffers:") != NULL, "/proc/meminfo has Buffers");
            CHECK(strstr(pbuf, "Cached:") != NULL, "/proc/meminfo has Cached");
        }
    }
    char selflink[64];
    ssize_t ll = readlink("/proc/self", selflink, sizeof(selflink) - 1);
    CHECK(ll > 0, "readlink(/proc/self) succeeds");
    if (ll > 0) {
        selflink[ll] = '\0';
        CHECK(strncmp(selflink, "/proc/", 6) == 0, "/proc/self points under /proc/");
        int alldigit = 1;
        for (const char *p = selflink + 6; *p; p++)
            if (*p < '0' || *p > '9')
                alldigit = 0;
        CHECK(alldigit && selflink[6] != '\0', "/proc/self target is a numeric pid");
    }
    fd = open("/proc/self/stat", O_RDONLY);
    CHECK(fd >= 0, "open(/proc/self/stat) succeeds");
    if (fd >= 0) {
        ssize_t m = read(fd, pbuf, sizeof(pbuf) - 1);
        close(fd);
        CHECK(m > 0, "/proc/self/stat non-empty");
    }

    /* ---- O_PATH + dirfd-relative openat (procps `top` chain) ---------- */
    errno = 0;
    int pfd = open("/proc/self", O_PATH | O_DIRECTORY);
    CHECK(pfd >= 0, "open(/proc/self, O_PATH|O_DIRECTORY) succeeds");
    if (pfd >= 0) {
        errno = 0;
        int s2 = openat(pfd, "stat", O_RDONLY);
        CHECK(s2 >= 0, "openat(/proc/self fd, \"stat\") succeeds");
        if (s2 >= 0) {
            ssize_t m = read(s2, pbuf, sizeof(pbuf) - 1);
            close(s2);
            CHECK(m > 0, "read(openat stat) non-empty");
            if (m > 0) {
                pbuf[m] = '\0';
                CHECK(pbuf[0] >= '1' && pbuf[0] <= '9',
                      "openat stat starts with own pid");
            }
        }
        errno = 0;
        char c;
        CHECK(read(pfd, &c, 1) == -1 && errno == EBADF,
              "read(O_PATH fd) -> EBADF");
        close(pfd);
    }
    errno = 0;
    int od = open("/proc/version", O_RDONLY | O_DIRECTORY);
    CHECK(od == -1 && errno == ENOTDIR, "open(file, O_DIRECTORY) -> ENOTDIR");
    if (od >= 0)
        close(od);
    int vf = open("/proc/version", O_RDONLY);
    CHECK(vf >= 0, "open(/proc/version) succeeds");
    if (vf >= 0) {
        errno = 0;
        int bad = openat(vf, "stat", O_RDONLY);
        CHECK(bad == -1 && errno == ENOTDIR, "openat(file fd, ...) -> ENOTDIR");
        if (bad >= 0)
            close(bad);
        close(vf);
    }

    /* ---- dup(2): lowest free fd (procps `top` hits nr 32) -------------- */
    errno = 0;
    int d0 = dup(1);
    CHECK(d0 >= 0, "dup(1) succeeds");
    if (d0 >= 0) {
        CHECK(d0 != 1, "dup returns a new fd");
        close(d0);
    }
    errno = 0;
    CHECK(dup(-1) == -1 && errno == EBADF, "dup(-1) -> EBADF");

    /* ---- clock_nanosleep(230): top's refresh delay ------------------- */
    errno = 0;
    struct timespec zreq = { 0, 0 };
    long zr = sys4(230, 1 /*CLOCK_MONOTONIC*/, 0, (unsigned long)&zreq, 0);
    CHECK(zr == 0, "clock_nanosleep(MONOTONIC, rel 0) == 0");
    errno = 0;
    struct timespec badts = { 0, 0 };
    /* Raw nr 230 returns -errno, not -1 (no libc to set errno). */
    long br = sys4(230, 99 /*bad clock*/, 0, (unsigned long)&badts, 0);
    CHECK(br == -EINVAL, "clock_nanosleep(bad clock) -> EINVAL");
    errno = 0;
    struct timespec past = { 0, 0 };
    long pr = sys4(230, 1 /*MONOTONIC*/, 1 /*TIMER_ABSTIME*/,
                   (unsigned long)&past, 0);
    CHECK(pr == 0, "clock_nanosleep(ABSTIME, past) == 0");
    errno = 0;
    CHECK(clock_nanosleep(CLOCK_MONOTONIC, 0, &zreq, NULL) == 0,
          "clock_nanosleep() wrapper still works");

    /* ---- sched_getaffinity(204): fastfetch/top probe ----------------- */
    errno = 0;
    cpu_set_t cpus;
    memset(&cpus, 0xaa, sizeof(cpus));
    CHECK(sched_getaffinity(0, sizeof(cpus), &cpus) == 0,
          "sched_getaffinity(0) succeeds");
    CHECK((((const unsigned char *)&cpus)[0] & 0x01) != 0,
          "sched_getaffinity has cpu 0 set");
    errno = 0;
    CHECK(sched_getaffinity(0, 0, &cpus) == -1 && errno == EINVAL,
          "sched_getaffinity(size 0) -> EINVAL");
    errno = 0;
    CHECK(sched_getaffinity(999999, sizeof(cpus), &cpus) == -1 && errno == ESRCH,
          "sched_getaffinity(bad pid) -> ESRCH");

    /* ---- capget(125): htop probe ------------------------------------- */
    errno = 0;
    struct __user_cap_header_struct caphdr = {
        _LINUX_CAPABILITY_VERSION_3, 0
    };
    struct __user_cap_data_struct capdata[2];
    memset(capdata, 0, sizeof(capdata));
    cap_user_header_t chdr = &caphdr;
    CHECK(sys2(125, (unsigned long)chdr, (unsigned long)capdata) == 0,
          "raw capget(v3, self) succeeds");
    CHECK((capdata[0].effective | capdata[0].permitted) != 0,
          "capget reports non-empty caps");
    errno = 0;
    struct __user_cap_header_struct badver = { 0xdeadbeef, 0 };
    CHECK(sys2(125, (unsigned long)&badver, (unsigned long)capdata) == -EINVAL,
          "capget(bad version) -> EINVAL");
    errno = 0;
    /* capset is a validated no-op: same header rules, always success. */
    CHECK(sys2(126, (unsigned long)chdr, (unsigned long)capdata) == 0,
          "raw capset(v3, self) succeeds");
    CHECK(sys2(126, (unsigned long)&badver, (unsigned long)capdata) == -EINVAL,
          "capset(bad version) -> EINVAL");

    /* ---- poll(7)/ppoll(271): readiness core --------------------------- */
    errno = 0;
    int pp[2];
    CHECK(pipe(pp) == 0, "pipe() for poll test");
    struct pollfd plf[2];
    plf[0].fd = pp[0];
    plf[0].events = POLLIN;
    plf[0].revents = 0;
    plf[1].fd = -1; /* negative fd: ignored, revents 0 */
    plf[1].events = POLLIN;
    plf[1].revents = 0x1234;
    CHECK(poll(plf, 2, 0) == 0, "poll(empty pipe, timeout 0) == 0");
    CHECK(plf[0].revents == 0, "poll sets revents 0 when not ready");
    CHECK(plf[1].revents == 0, "poll clears revents of negative fd");
    errno = 0;
    const char wch = 'x';
    CHECK(write(pp[1], &wch, 1) == 1, "pipe write for poll test");
    CHECK(poll(plf, 2, 0) == 1, "poll(full pipe, timeout 0) == 1");
    CHECK((plf[0].revents & POLLIN) != 0, "poll reports POLLIN on data");
    errno = 0;
    char rch = 0;
    CHECK(read(pp[0], &rch, 1) == 1 && rch == 'x', "pipe read back");
    CHECK(poll(plf, 1, 0) == 0, "poll(drained pipe) == 0");
    errno = 0;
    struct pollfd badfd = { 9999, POLLIN, 0 };
    CHECK(poll(&badfd, 1, 0) == 1, "poll(bad fd) reports 1 event");
    CHECK((badfd.revents & POLLNVAL) != 0, "poll(bad fd) -> POLLNVAL");
    errno = 0;
    struct timespec pts = { 0, 0 };
    plf[0].revents = 0;
    CHECK(write(pp[1], &wch, 1) == 1, "pipe write for ppoll test");
    CHECK(ppoll(plf, 1, &pts, NULL) == 1, "ppoll(data ready) == 1");
    CHECK(read(pp[0], &rch, 1) == 1, "pipe drain for ppoll test");
    close(pp[0]);
    close(pp[1]);

    /* ---- select(23)/pselect6(270) -------------------------------------- */
    errno = 0;
    int sp[2];
    CHECK(pipe(sp) == 0, "pipe() for select test");
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(sp[0], &rs);
    struct timeval tv0 = { 0, 0 };
    CHECK(select(sp[0] + 1, &rs, NULL, NULL, &tv0) == 0,
          "select(empty pipe, tv 0) == 0");
    CHECK(!FD_ISSET(sp[0], &rs), "select clears bit when not ready");
    CHECK(write(sp[1], &wch, 1) == 1, "pipe write for select test");
    FD_ZERO(&rs);
    FD_SET(sp[0], &rs);
    CHECK(select(sp[0] + 1, &rs, NULL, NULL, &tv0) == 1,
          "select(full pipe) == 1");
    CHECK(FD_ISSET(sp[0], &rs), "select sets bit when ready");
    CHECK(read(sp[0], &rch, 1) == 1, "pipe drain for select test");
    errno = 0;
    int bp[2];
    CHECK(pipe(bp) == 0, "pipe() for select EBADF test");
    close(bp[0]); /* now bp[0] is closed but in range */
    FD_ZERO(&rs);
    FD_SET(bp[0], &rs);
    CHECK(select(bp[0] + 1, &rs, NULL, NULL, &tv0) == -1 && errno == EBADF,
          "select(closed fd) -> EBADF");
    close(bp[1]);
    errno = 0;
    FD_ZERO(&rs);
    FD_SET(sp[0], &rs);
    struct timespec sts = { 0, 0 };
    CHECK(pselect(sp[0] + 1, &rs, NULL, NULL, &sts, NULL) == 0,
          "pselect(empty pipe) == 0");
    close(sp[0]);
    close(sp[1]);

    /* ---- epoll(213/291/233/232) ---------------------------------------- */
    errno = 0;
    int efd = epoll_create1(0);
    CHECK(efd >= 0, "epoll_create1(0) succeeds");
    errno = 0;
    CHECK(epoll_create1(0xdead) == -1 && errno == EINVAL,
          "epoll_create1(bad flags) -> EINVAL");
    errno = 0;
    int ep2[2];
    CHECK(pipe(ep2) == 0, "pipe() for epoll test");
    struct epoll_event eev;
    eev.events = EPOLLIN;
    eev.data.u64 = 0x12345678u;
    CHECK(epoll_ctl(efd, EPOLL_CTL_ADD, ep2[0], &eev) == 0,
          "epoll_ctl(ADD pipe) succeeds");
    errno = 0;
    CHECK(epoll_ctl(efd, EPOLL_CTL_ADD, ep2[0], &eev) == -1 && errno == EEXIST,
          "epoll_ctl(ADD twice) -> EEXIST");
    errno = 0;
    struct epoll_event got[4];
    CHECK(epoll_wait(efd, got, 4, 0) == 0, "epoll_wait(empty pipe, t=0) == 0");
    CHECK(write(ep2[1], &wch, 1) == 1, "pipe write for epoll test");
    errno = 0;
    int nev = epoll_wait(efd, got, 4, 0);
    CHECK(nev == 1, "epoll_wait(full pipe) == 1");
    if (nev == 1) {
        CHECK((got[0].events & EPOLLIN) != 0, "epoll reports EPOLLIN");
        CHECK(got[0].data.u64 == 0x12345678u, "epoll data round-trips");
    }
    errno = 0;
    CHECK(epoll_ctl(efd, EPOLL_CTL_DEL, ep2[0], NULL) == 0,
          "epoll_ctl(DEL) succeeds");
    CHECK(epoll_ctl(efd, EPOLL_CTL_DEL, ep2[0], NULL) == -1 && errno == ENOENT,
          "epoll_ctl(DEL twice) -> ENOENT");
    errno = 0;
    CHECK(epoll_wait(efd, got, 0, 0) == -1 && errno == EINVAL,
          "epoll_wait(maxevents 0) -> EINVAL");
    CHECK(read(ep2[0], &rch, 1) == 1, "pipe drain for epoll test");
    close(ep2[0]);
    close(ep2[1]);
    close(efd);

    /* ---- /proc/loadavg -------------------------------------------------- */
    errno = 0;
    int lfd = open("/proc/loadavg", O_RDONLY);
    CHECK(lfd >= 0, "open(/proc/loadavg) succeeds");
    if (lfd >= 0) {
        char lbuf[64];
        ssize_t m = read(lfd, lbuf, sizeof(lbuf) - 1);
        close(lfd);
        CHECK(m > 0, "/proc/loadavg non-empty");
        if (m > 0) {
            lbuf[m] = '\0';
            double a = -1, b = -1, c = -1;
            CHECK(sscanf(lbuf, "%lf %lf %lf", &a, &b, &c) == 3,
                  "/proc/loadavg has three averages");
            CHECK(a == 0.0 && b == 0.0 && c == 0.0,
                  "/proc/loadavg averages are 0.00");
            CHECK(strchr(lbuf, '/') != NULL, "/proc/loadavg has running/total");
        }
    }

    /* ---- htop-style /proc scan (openat dirfd chains) ------------------ */
    errno = 0;
    int procfd = open("/proc", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    CHECK(procfd >= 0, "htop-mimic: open(/proc, O_DIRECTORY|O_NOFOLLOW)");
    DIR *pd = NULL;
    if (procfd >= 0) {
        pd = fdopendir(procfd);
        CHECK(pd != NULL, "htop-mimic: fdopendir(/proc)");
    }
    int found = 0, opened = 0, statm_ok = 0, stat_ok = 0;
    if (pd) {
        struct dirent *e;
        while ((e = readdir(pd)) != NULL) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9')
                continue;
            found++;
            int pfd = openat(procfd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (pfd < 0)
                continue;
            opened++;
            int sfd = openat(pfd, "statm", O_RDONLY);
            if (sfd >= 0) {
                char sb[128];
                ssize_t m = read(sfd, sb, sizeof(sb) - 1);
                if (m > 0) {
                    sb[m] = 0;
                    long a, b, c, d, f, g, h;
                    if (sscanf(sb, "%ld %ld %ld %ld %ld %ld %ld",
                               &a, &b, &c, &d, &f, &g, &h) == 7)
                        statm_ok++;
                }
                close(sfd);
            }
            int tfd = openat(pfd, "stat", O_RDONLY);
            if (tfd >= 0) {
                char sb2[1024];
                if (read(tfd, sb2, sizeof(sb2) - 1) > 0)
                    stat_ok++;
                close(tfd);
            }
            close(pfd);
        }
        closedir(pd);
    }
    CHECK(found > 0, "htop-mimic: readdir finds pid dirs");
    CHECK(opened > 0, "htop-mimic: openat(pid, O_DIRECTORY|O_NOFOLLOW)");
    CHECK(statm_ok > 0, "htop-mimic: statm parses 7 fields");
    CHECK(stat_ok > 0, "htop-mimic: pid stat readable");

    /* ---- htop thread scan: task/<tid>/stat + cpuN lines ------------- */
    /* (closedir above closed procfd too: reopen for the dirfd chains.) */
    errno = 0;
    int procfd2 = open("/proc", O_RDONLY | O_DIRECTORY);
    CHECK(procfd2 >= 0, "htop-mimic: reopen /proc");
    int tsk = -1;
    if (procfd2 >= 0)
        tsk = openat(procfd2, "1/task", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (tsk < 0)
        tsk = open("/proc/1/task", O_RDONLY | O_DIRECTORY);
    CHECK(tsk >= 0, "htop-mimic: open(/proc/1/task)");
    int tstat_ok = 0;
    if (tsk >= 0) {
        /* main-thread alias: task/<tgid>/stat must serve the pid render. */
        int afd = openat(tsk, "1/stat", O_RDONLY);
        if (afd >= 0) {
            char sb[1024];
            if (read(afd, sb, sizeof(sb) - 1) > 0)
                tstat_ok = 1;
            close(afd);
        }
        DIR *td = fdopendir(tsk);
        if (td) {
            struct dirent *e;
            while ((e = readdir(td)) != NULL) {
                if (e->d_name[0] < '0' || e->d_name[0] > '9')
                    continue;
                char sub[64];
                snprintf(sub, sizeof(sub), "%s/stat", e->d_name);
                int tsfd = openat(tsk, sub, O_RDONLY);
                if (tsfd >= 0) {
                    char sb[1024];
                    if (read(tsfd, sb, sizeof(sb) - 1) > 0)
                        tstat_ok++;
                    close(tsfd);
                    break; /* one thread entry is enough */
                }
            }
            closedir(td);
        } else {
            close(tsk);
        }
    }
    CHECK(tstat_ok > 0, "htop-mimic: task/<tid>/stat readable");
    errno = 0;
    int sfd2 = open("/proc/stat", O_RDONLY);
    CHECK(sfd2 >= 0, "open(/proc/stat) for cpuN check");
    int cpuN_ok = 0;
    if (sfd2 >= 0) {
        char sb[1024];
        ssize_t m = read(sfd2, sb, sizeof(sb) - 1);
        close(sfd2);
        if (m > 0) {
            sb[m] = 0;
            cpuN_ok = strstr(sb, "cpu0 ") != NULL;
        }
    }
    CHECK(cpuN_ok, "/proc/stat has per-CPU cpuN lines");
    if (procfd2 >= 0)
        close(procfd2);

    /* ---- getpriority(140)/setpriority(141): htop NI column + F7/F8 ---- */
    errno = 0;
    CHECK(getpriority(PRIO_PROCESS, 0) == 0, "getpriority(self) == 0");
    errno = 0;
    CHECK(setpriority(PRIO_PROCESS, 0, 5) == 0, "setpriority(self, 5) == 0");
    errno = 0;
    CHECK(getpriority(PRIO_PROCESS, 0) == 5, "getpriority(self) == 5 after set");
    errno = 0;
    CHECK(setpriority(PRIO_PROCESS, 0, 0) == 0, "setpriority(self, 0) restores");
    errno = 0;
    CHECK(setpriority(PRIO_PROCESS, 0, 99) == -1 && errno == EINVAL,
          "setpriority(prio 99) -> EINVAL");
    errno = 0;
    /* NOTE: mlibc's getpriority returns the value slot (0 on error), not -1:
     * -1 is a legal nice value, so errors surface via errno only. */
    CHECK(getpriority(99, 0) == 0 && errno == EINVAL,
          "getpriority(bad which) -> EINVAL");
    errno = 0;
    CHECK(getpriority(PRIO_PROCESS, 999999) == 0 && errno == ESRCH,
          "getpriority(bad pid) -> ESRCH");

    /* ---- kill(2) delivery: fork, SIGTERM, reap ----------------------- */
    errno = 0;
    pid_t kc = fork();
    CHECK(kc >= 0, "fork() for kill test");
    if (kc == 0) {
        /* child: block until a signal arrives (default dispositions kill).
         * sleep() here is nanosleep-backed; pause() needs a Pause sysdep
         * this kernel does not provide. */
        for (;;)
            sleep(1000);
        _exit(0);
    }
    if (kc > 0) {
        struct timespec ks = { 0, 50 * 1000 * 1000 };
        nanosleep(&ks, NULL); /* let the child park in pause() */
        errno = 0;
        CHECK(kill(kc, 0) == 0, "kill(child, 0) probe succeeds");
        errno = 0;
        CHECK(kill(kc, SIGTERM) == 0, "kill(child, SIGTERM) succeeds");
        int kst = 0;
        pid_t kw = waitpid(kc, &kst, 0);
        CHECK(kw == kc && WIFSIGNALED(kst) && WTERMSIG(kst) == SIGTERM,
              "child reaped as terminated by SIGTERM");
        errno = 0;
        CHECK(kill(kc, 0) == -1 && errno == ESRCH,
              "kill(reaped, 0) -> ESRCH");
    }

    /* ---- VT emulation smoke (no /proc/vtdbg since its removal) ------ */
    /* The grid dump is gone from the kernel; these writes still exercise
     * the parser (cup/EL/region/IL/DL/stray/?7/?1 paths) and must not
     * error or hang. Visual correctness was verified live in htop. */
    errno = 0;
    int tty1 = open("/dev/tty2", O_WRONLY);
    CHECK(tty1 >= 0, "vt: open(/dev/tty2)");
    if (tty1 >= 0) {
        /* clear + absolute cursor + text */
        const char *s1 = "\x1b[2J\x1b[1;1Hhello\x1b[5;10Hworld";
        CHECK(write(tty1, s1, strlen(s1)) == (ssize_t)strlen(s1), "vt: basic write");
        /* EL: row0 "abcdefgh", back to col 4, erase to EOL */
        const char *s2 = "\x1b[1;1Habcdefgh\x1b[1;4H\x1b[K";
        CHECK(write(tty1, s2, strlen(s2)) == (ssize_t)strlen(s2), "vt: EL write");
        /* scroll region rows 10-12 + newline at bottom margin */
        const char *s3 = "\x1b[10;12r\x1b[10;1HAAA\x1b[11;1HBBB\x1b[12;1HCCC\x1b[12;1H\n\x1b[r";
        CHECK(write(tty1, s3, strlen(s3)) == (ssize_t)strlen(s3), "vt: region write");
        /* IL at row 11, then reset region */
        const char *s5 = "\x1b[10;12r\x1b[10;1H111\x1b[11;1H222\x1b[12;1H333\x1b[11;1H\x1b[L\x1b[r";
        CHECK(write(tty1, s5, strlen(s5)) == (ssize_t)strlen(s5), "vt: IL write");
        /* DL row 11 */
        const char *s7 = "\x1b[10;12r\x1b[11;1H\x1b[M\x1b[r";
        CHECK(write(tty1, s7, strlen(s7)) == (ssize_t)strlen(s7), "vt: DL write");
        /* stray-print suppression: ESC = / ESC > / ESC ( B emit nothing */
        const char *s8 = "\x1b[2J\x1b[1;1H\x1b=\x1b>\x1b(BZ";
        CHECK(write(tty1, s8, strlen(s8)) == (ssize_t)strlen(s8), "vt: stray write");
        /* ?7l: 200 X's in one row, never wrap; then ?1h/?1l roundtrip */
        char s9[240];
        int sl = 0;
        sl += snprintf(s9 + sl, sizeof(s9) - sl, "\x1b[20;1H\x1b[?7l");
        for (int i = 0; i < 200; i++)
            s9[sl++] = 'X';
        sl += snprintf(s9 + sl, sizeof(s9) - sl, "\x1b[?7h\x1b[r\x1b[?1h\x1b[5;5Hq\x1b[?1l");
        CHECK(write(tty1, s9, sl) == sl, "vt: nowrap+DECCKM write");
        close(tty1);
    }

    if (g_fails == 0) {
        printf("LINUX-OK\n");
        return 0;
    }
    printf("LINUX-FAIL: %d check(s) failed\n", g_fails);
    return 1;
}
