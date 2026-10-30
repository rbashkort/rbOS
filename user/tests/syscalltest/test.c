#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static long sys_call(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

/* Read /proc/enosys into buf (NUL-terminated). Returns 0 on success. */
static int read_enosys(char *buf, size_t cap) {
    int fd = open("/proc/enosys", O_RDONLY);
    if (fd < 0)
        return -1;
    size_t off = 0;
    while (off + 1 < cap) {
        ssize_t n = read(fd, buf + off, cap - 1 - off);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    close(fd);
    buf[off] = '\0';
    return 0;
}

/* Sum the counts of lines whose first field is exactly key. */
static unsigned long count_key(const char *buf, const char *key) {
    size_t klen = strlen(key);
    unsigned long total = 0;
    const char *p = buf;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        if (!nl)
            break;
        if ((size_t)(nl - p) > klen && strncmp(p, key, klen) == 0 &&
            p[klen] == ' ')
            total += strtoul(p + klen + 1, NULL, 10);
        p = nl + 1;
    }
    return total;
}

int main(void) {
    struct utsname u;
    if (uname(&u) == 0)
        printf("[ok] uname: %s %s %s (%s)\n", u.sysname, u.release, u.machine, u.version);
    else
        printf("[FAIL] uname\n");

    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        printf("[ok] gettimeofday: %ld.%06ld\n", (long)tv.tv_sec, (long)tv.tv_usec);
    else
        printf("[FAIL] gettimeofday\n");

    printf("[ok] time: %ld\n", (long)time(NULL));

    if (access("/bin/sh", F_OK) == 0)
        printf("[ok] access /bin/sh exists\n");
    else
        printf("[FAIL] access /bin/sh\n");

    if (access("/definitely/not/here", F_OK) != 0)
        printf("[ok] access missing file fails as expected\n");
    else
        printf("[FAIL] access missing file\n");

    char buf[256];
    ssize_t n = readlink("/bin", buf, sizeof(buf) - 1);
    if (n >= 0) {
        buf[n] = '\0';
        printf("[ok] readlink /bin -> %s\n", buf);
    } else {
        printf("[FAIL] readlink /bin errno=%ld\n", -n);
    }

    unsigned char rnd[8];
    long r = sys_call(318 /* getrandom */, (long)rnd, sizeof(rnd), 0);
    if (r == 8) {
        printf("[ok] getrandom:");
        for (int i = 0; i < 8; i++) printf(" %02x", rnd[i]);
        printf("\n");
    } else {
        printf("[FAIL] getrandom ret=%ld\n", r);
    }

    /* P7.0a: the P0 table must cover the whole extension range (1777..2047);
     * nr=2048 is the first overflow slot. A 1024-entry table would put 2047
     * into the overflow line instead of its own "2047 <count>" line. */
    static char enosys_buf[65536];
    unsigned long before_2047 = 0, before_ov = 0;
    if (read_enosys(enosys_buf, sizeof(enosys_buf)) == 0) {
        before_2047 = count_key(enosys_buf, "2047");
        before_ov = count_key(enosys_buf, "overflow");
    } else {
        printf("[FAIL] /proc/enosys unreadable\n");
    }

    long r2047 = sys_call(2047, 0, 0, 0);
    long r2048 = sys_call(2048, 0, 0, 0);
    if (r2047 != -ENOSYS || r2048 != -ENOSYS)
        printf("[FAIL] enosys boundary ret: 2047=%ld 2048=%ld (want %d)\n",
               r2047, r2048, -ENOSYS);

    if (read_enosys(enosys_buf, sizeof(enosys_buf)) == 0) {
        unsigned long after_2047 = count_key(enosys_buf, "2047");
        unsigned long after_ov = count_key(enosys_buf, "overflow");
        if (after_2047 <= before_2047)
            printf("[FAIL] enosys 2047 not tracked (before=%lu after=%lu)\n",
                   before_2047, after_2047);
        if (after_ov <= before_ov)
            printf("[FAIL] enosys overflow not counted (before=%lu after=%lu)\n",
                   before_ov, after_ov);
        if (after_2047 > before_2047 && after_ov > before_ov)
            printf("[ok] enosys boundary: 2047 tracked, 2048 overflow\n");
    }

    return 0;
}
