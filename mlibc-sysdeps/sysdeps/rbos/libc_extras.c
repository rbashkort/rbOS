/* Public ioctl(2) for rbOS mlibc.
 *
 * Lives in its own translation unit so that it is compiled ONLY into libc
 * (generic.cpp is compiled twice - libc and rtld flavors - and must keep an
 * identical symbol set in both to avoid duplicate archive members clashing
 * at link time). Needs TLS errno, which the rtld flavor cannot provide.
 *
 * With the glibc option enabled (P3.6) mlibc ships its own ioctl() in
 * options/glibc/generic/sys-ioctl.cpp, so this fallback is compiled out to
 * avoid a duplicate definition. */

#include <mlibc-config.h>

#if !__MLIBC_GLIBC_OPTION

#include <errno.h>
#include <stdarg.h>

static long rbos_raw_syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                      : "rcx", "r11", "memory");
    return ret;
}

int ioctl(int fd, unsigned long request, ...) {
    void *arg;
    va_list ap;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    /* RB_SYS_IOCTL = 16; kernel returns -errno on failure. */
    long ret = rbos_raw_syscall3(16, fd, (long)request, (long)arg);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

#endif /* !__MLIBC_GLIBC_OPTION */
