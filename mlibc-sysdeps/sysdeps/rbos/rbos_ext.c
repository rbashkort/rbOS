/* P7: rbOS opt-in OpenBSD-style extensions.
 *
 * This translation unit is compiled into libc only (like libc_extras.c):
 * generic.cpp is shared with rtld, and the rtld flavor has no TLS errno.
 * The syscall numbers live in the kernel's canonical ABI header, so there is
 * exactly one definition of 1777/1778 in the tree.
 */

#include "../../../kernel/include/syscall_abi.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static long rbos_raw_syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1), "S"(a2)
                      : "rcx", "r11", "memory");
    return ret;
}

int pledge(const char *promises, const char *execpromises) {
    long ret = rbos_raw_syscall2(RB_SYS_PLEDGE,
                                 (long)(uintptr_t)promises,
                                 (long)(uintptr_t)execpromises);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return 0;
}

int unveil(const char *path, const char *permissions) {
    long ret = rbos_raw_syscall2(RB_SYS_UNVEIL,
                                 (long)(uintptr_t)path,
                                 (long)(uintptr_t)permissions);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return 0;
}

/* getentropy(2) accepts at most 256 bytes per call on Linux; loop so the
 * arc4random API keeps its "fills the whole buffer" contract. There is no
 * error channel in these functions, so a non-EINTR entropy failure is fatal
 * (the process cannot safely continue with predictable randomness). */
static void rbos_arc4random_buf(void *buf, size_t len) {
    unsigned char *p = (unsigned char *)buf;
    while (len != 0) {
        size_t chunk = len > 256 ? 256 : len;
        if (getentropy(p, chunk) != 0) {
            if (errno == EINTR)
                continue;
            __builtin_trap();
        }
        p += chunk;
        len -= chunk;
    }
}

uint32_t arc4random(void) {
    uint32_t value;
    rbos_arc4random_buf(&value, sizeof(value));
    return value;
}

void arc4random_buf(void *buf, size_t len) {
    rbos_arc4random_buf(buf, len);
}

uint32_t arc4random_uniform(uint32_t upper_bound) {
    if (upper_bound < 2)
        return 0;

    /* Rejection sampling avoids modulo bias: discard values below
     * 2^32 mod upper_bound. */
    uint32_t min = (uint32_t)(-upper_bound) % upper_bound;
    uint32_t value;
    do {
        value = arc4random();
    } while (value < min);
    return value % upper_bound;
}
