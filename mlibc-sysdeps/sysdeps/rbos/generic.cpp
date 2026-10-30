#include <bits/ensure.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <mlibc/all-sysdeps.hpp>
#include <mlibc/debug.hpp>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/resource.h>
#include <fcntl.h>
#include "../../../kernel/include/syscall_abi.h"

// Signal restorer trampoline (Linux-style): the kernel redirects a signal
// handler's return address here; RB_SYS_SIGRETURN restores the interrupted
// context from the sigframe.
//
// The symbol MUST be hidden: ld.so also links this file, and a pre-emptible
// interposable symbol would emit an R_X86_64_GLOB_DAT dynamic relocation,
// which relocateSelf() cannot process (it only handles R_X86_64_RELATIVE).
extern "C" __attribute__((visibility("hidden"))) void rbos_sigrestore(void);

__asm__(
    ".text\n"
    ".globl rbos_sigrestore\n"
    ".hidden rbos_sigrestore\n"
    ".type rbos_sigrestore, @function\n"
    "rbos_sigrestore:\n"
    "    mov $15, %rax\n"   // RB_SYS_SIGRETURN
    "    syscall\n"
    "    ud2\n"
);

// x86_64 syscall wrapper using inline assembly
static inline int64_t syscall0(uint64_t n) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall1(uint64_t n, uint64_t a1) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall2(uint64_t n, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall4(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall5(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t syscall6(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;
    register uint64_t r9 __asm__("r9") = a6;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

namespace mlibc {

// Debug logging - writes to stderr
void Sysdeps<LibcLog>::operator()(const char *message) {
    ssize_t len = strlen(message);
    syscall3(RB_SYS_WRITE, 2, reinterpret_cast<uint64_t>(message), len);
}

// Panic handler
void Sysdeps<LibcPanic>::operator()() {
    sysdep<LibcLog>("mlibc panic!\n");
    sysdep<Exit>(1);
    __builtin_unreachable();
}

// Exit process
void Sysdeps<Exit>::operator()(int status) {
    syscall1(RB_SYS_EXIT, status);
    __builtin_unreachable();
}

// Anonymous memory allocation (for malloc backing)
int Sysdeps<AnonAllocate>::operator()(size_t size, void **pointer) {
    int64_t result = syscall6(RB_SYS_MMAP, 0, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    
    if (result < 0) {
        return static_cast<int>(-result);
    }
    
    *pointer = reinterpret_cast<void*>(result);
    return 0;
}

// Free anonymous memory
int Sysdeps<AnonFree>::operator()(void *pointer, size_t size) {
    int64_t result = syscall2(RB_SYS_MUNMAP, reinterpret_cast<uint64_t>(pointer), size);
    
    if (result < 0) {
        return static_cast<int>(-result);
    }
    
    return 0;
}

// Virtual memory management
int Sysdeps<VmMap>::operator()(void *hint, size_t size, int prot, int flags, int fd, off_t offset, void **window) {
    int64_t result = syscall6(RB_SYS_MMAP, reinterpret_cast<uint64_t>(hint), size, prot, flags, fd, offset);
    
    if (result < 0) {
        return static_cast<int>(-result);
    }
    
    *window = reinterpret_cast<void*>(result);
    return 0;
}

// File operations
int Sysdeps<Open>::operator()(const char *path, int flags, mode_t mode, int *fd) {
    int64_t result = syscall3(RB_SYS_OPEN, reinterpret_cast<uint64_t>(path), flags, mode);

    if (result < 0) {
        return static_cast<int>(-result);
    }

    *fd = static_cast<int>(result);
    return 0;
}

int Sysdeps<Close>::operator()(int fd) {
    int64_t result = syscall1(RB_SYS_CLOSE, fd);
    
    if (result < 0) {
        return static_cast<int>(-result);
    }
    
    return 0;
}

int Sysdeps<Read>::operator()(int fd, void *buf, size_t count, ssize_t *bytes_read) {
    int64_t result = syscall3(RB_SYS_READ, fd, reinterpret_cast<uint64_t>(buf), count);
    
    if (result < 0) {
        return static_cast<int>(-result);
    }
    
    *bytes_read = result;
    return 0;
}

int Sysdeps<Write>::operator()(int fd, const void *buf, size_t count, ssize_t *bytes_written) {
    int64_t result = syscall3(RB_SYS_WRITE, fd, reinterpret_cast<uint64_t>(buf), count);
    
    if (result < 0) {
        return static_cast<int>(-result);
    }
    
    *bytes_written = result;
    return 0;
}

int Sysdeps<Seek>::operator()(int fd, off_t offset, int whence, off_t *new_offset) {
    int64_t result = syscall3(RB_SYS_LSEEK, fd, offset, whence);
    
    if (result < 0) {
        return static_cast<int>(-result);
    }
    
    *new_offset = result;
    return 0;
}

// Time functions

struct timespec_buf {
    int64_t tv_sec;
    int64_t tv_nsec;
};

int Sysdeps<ClockGet>::operator()(int clock, time_t *secs, long *nanos) {
    timespec_buf ts{};
    int64_t res = syscall2(RB_SYS_CLOCK_GETTIME, clock, reinterpret_cast<uint64_t>(&ts));
    if (res < 0) {
        return static_cast<int>(-res);
    }
    *secs = ts.tv_sec;
    *nanos = ts.tv_nsec;
    return 0;
}

int Sysdeps<Sleep>::operator()(time_t *secs, long *nanos) {
    timespec_buf req{
        static_cast<int64_t>(secs ? *secs : 0),
        static_cast<int64_t>(nanos ? *nanos : 0)
    };
    timespec_buf rem{0, 0};

    int64_t res = syscall2(RB_SYS_NANOSLEEP, reinterpret_cast<uint64_t>(&req), reinterpret_cast<uint64_t>(&rem));
    if (res < 0) {
        return static_cast<int>(-res);
    }

    if (secs) *secs = rem.tv_sec;
    if (nanos) *nanos = rem.tv_nsec;

    return 0;
}

// TTY check: probe with TCGETS so any fd referring to a terminal counts,
// regardless of its number.
int Sysdeps<Isatty>::operator()(int fd) {
    struct termios attr;
    int dummy = 0;
    if (sysdep<Ioctl>(fd, 0x5401 /* TCGETS */, &attr, &dummy) == 0) {
        return 0;
    }
    return ENOTTY;
}

int Sysdeps<Ioctl>::operator()(int fd, unsigned long request, void *arg, int *result) {
    int64_t ret = syscall3(RB_SYS_IOCTL, fd, request, reinterpret_cast<uint64_t>(arg));
    if (ret < 0) return static_cast<int>(-ret);
    *result = static_cast<int>(ret);
    return 0;
}

// Public ioctl(2): implemented in libc_extras.c (needs TLS errno, which is
// unavailable in the rtld flavor of generic.cpp).

int Sysdeps<Ttyname>::operator()(int fd, char *buf, size_t size) {
    if (fd < 0 || fd > 2) return EBADF;

    const char target[] = "/dev/tty0";
    size_t len = sizeof(target);
    if (size < len) return ERANGE;

    for (size_t i = 0; i < len; i++) {
        buf[i] = target[i];
    }
    return 0;
}

int Sysdeps<Tcgetattr>::operator()(int fd, struct termios *attr) {
    int dummy = 0;
    return sysdep<Ioctl>(fd, 0x5401 /* TCGETS */, attr, &dummy);
}

int Sysdeps<Tcsetattr>::operator()(int fd, int optional_actions, const struct termios *attr) {
    // optional_actions is the TCSANOW/TCSADRAIN/TCSAFLUSH selector, not a
    // decoration: TCSAFLUSH must discard queued input, TCSADRAIN waits for
    // the output queue. Forward the matching Linux request instead of always
    // sending TCSETS, which silently drops both guarantees.
    unsigned long request;
    switch(optional_actions) {
    case TCSADRAIN: request = 0x5403 /* TCSETSW */; break;
    case TCSAFLUSH: request = 0x5404 /* TCSETSF */; break;
    case TCSANOW:
    default: request = 0x5402 /* TCSETS */; break;
    }
    int dummy = 0;
    return sysdep<Ioctl>(fd, request, const_cast<struct termios*>(attr), &dummy);
}

// uname(2)
int Sysdeps<Uname>::operator()(struct utsname *buf) {
    int64_t res = syscall1(RB_SYS_UNAME /* 63 */, reinterpret_cast<uint64_t>(buf));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// readlink(2)
int Sysdeps<Readlink>::operator()(const char *path, void *buffer, size_t max_size, ssize_t *length) {
    int64_t res = syscall3(RB_SYS_READLINK /* 89 */,
                           reinterpret_cast<uint64_t>(path),
                           reinterpret_cast<uint64_t>(buffer),
                           static_cast<uint64_t>(max_size));
    if (res < 0) return static_cast<int>(-res);
    if (length) *length = static_cast<ssize_t>(res);
    return 0;
}

// kill(2)
int Sysdeps<Kill>::operator()(pid_t pid, int sig) {
    int64_t res = syscall2(RB_SYS_KILL /* 62 */, static_cast<uint64_t>(pid), static_cast<uint64_t>(sig));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// Thread Control Block
int Sysdeps<TcbSet>::operator()(void *pointer) {
    int64_t res = syscall2(RB_SYS_ARCH_PRCTL, 0x1002, reinterpret_cast<uint64_t>(pointer));
    if (res < 0) {
        return -res;
    }
    return 0;
}
// Futexes: real kernel-backed implementation (RB_SYS_FUTEX, nr 202).
#define RB_FUTEX_WAIT 0
#define RB_FUTEX_WAKE 1

int Sysdeps<FutexWait>::operator()(int *pointer, int expected, const struct timespec *time) {
    // `time` is a *relative* timeout: mlibc converts an absolute deadline to
    // relative before calling us (see thread_mutex_timedlock / condvar). The
    // kernel honours it (P4.1); a null pointer means "wait forever".
    int64_t res = syscall4(RB_SYS_FUTEX /* 202 */,
                           reinterpret_cast<uint64_t>(pointer),
                           static_cast<uint64_t>(RB_FUTEX_WAIT),
                           static_cast<uint64_t>(expected),
                           reinterpret_cast<uint64_t>(time));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<FutexWake>::operator()(int *pointer, bool all) {
    int64_t res = syscall4(RB_SYS_FUTEX /* 202 */,
                           reinterpret_cast<uint64_t>(pointer),
                           static_cast<uint64_t>(RB_FUTEX_WAKE),
                           all ? 0x7FFFFFFF : 1,
                           0);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// ---- threads (pthread) ----
//
// NOTE: __mlibc_enter_thread lives in enter_thread.cpp; including
// <mlibc/tcb.hpp> from this TU breaks the frigg/freestnd include chain.
extern "C" long __mlibc_spawn_thread(unsigned long flags, void *stack, void *pid_out, void *child_tid, void *tls);

// clone(2) flags (Linux ABI). Taken from the shared abi-bits header: defining
// them locally as well would clash once another libc header includes them.
#include <abi-bits/clone-flags.h>

int Sysdeps<Clone>::operator()(void *tcb, pid_t *pid_out, void *stack) {
	unsigned long flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND
		| CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS
		| CLONE_PARENT_SETTID;
	// Asm wrapper follows the Linux x86-64 clone ABI:
	// (flags, stack, parent_tid, child_tid, tls). It moves child_tid (NULL)
	// into r10 and leaves tls in r8; in the child it pops {entry, user_arg}
	// from the prepared stack into enter_thread().
	long res = __mlibc_spawn_thread(flags, stack, pid_out, nullptr, tcb);
	if (res < 0) return static_cast<int>(-res);
	return 0;
}

int Sysdeps<PrepareStack>::operator()(void **stack, void *entry, void *user_arg, void* tcb, size_t *stack_size, size_t *guard_size, void **stack_base) {
    (void)tcb;
    static constexpr size_t default_stacksize = 0x200000;

    if (!*stack_size)
        *stack_size = default_stacksize;

    void *map;
    if (*stack) {
        map = *stack;
        *guard_size = 0;
    } else {
        auto mmap_result = mlibc::sysdep<VmMap>(
            nullptr, *stack_size + *guard_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, &map
        );
        if (mmap_result || reinterpret_cast<void*>(map) == MAP_FAILED)
            return EAGAIN;
        if (*guard_size) {
            // Turn the low part into an unmapped guard region.
            int ret = mlibc::sysdep<VmProtect>(
                reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(map)), *guard_size, PROT_NONE
            );
            if (ret)
                return EAGAIN;
        }
    }

    *stack_base = reinterpret_cast<void*>(map);
    auto sp = reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(map) + *guard_size + *stack_size);
    *--sp = reinterpret_cast<uintptr_t>(user_arg);
    *--sp = reinterpret_cast<uintptr_t>(entry);
    *stack = reinterpret_cast<void*>(sp);
    return 0;
}

// Thread exit: exit(60) terminates just this thread (the kernel turns the
// process into a zombie when its last thread goes away).
[[noreturn]] void Sysdeps<ThreadExit>::operator()() {
    syscall1(RB_SYS_EXIT /* 60 */, 0);
    __builtin_trap();
}

//

int Sysdeps<Fork>::operator()(pid_t *child) {
    int64_t res = syscall0(RB_SYS_FORK);
    if (res < 0) {
        return -res;
    }
    *child = static_cast<pid_t>(res);
    return 0;
}

//

int Sysdeps<Stat>::operator()(fsfd_target target, int fd, const char *path, int flags, struct stat *statbuf) {
    if (target == fsfd_target::fd) {
        int64_t res = syscall2(RB_SYS_FSTAT, static_cast<uint64_t>(fd), reinterpret_cast<uint64_t>(statbuf));
        if (res < 0) return static_cast<int>(-res);
        return 0;
    }

    // path and fd_path both go through fstatat with the caller's AT_* flags.
    int dirfd = (target == fsfd_target::fd_path) ? fd : -100 /* AT_FDCWD */;
    int64_t res = syscall4(RB_SYS_NEWFSTATAT /* 262 */,
                           static_cast<uint64_t>(dirfd),
                           reinterpret_cast<uint64_t>(path),
                           reinterpret_cast<uint64_t>(statbuf),
                           static_cast<uint64_t>(flags));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// Directories & Working Directory
int Sysdeps<GetCwd>::operator()(char *buffer, size_t size) {
    int64_t res = syscall2(RB_SYS_GETCWD, reinterpret_cast<uint64_t>(buffer), size);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Chdir>::operator()(const char *path) {
    int64_t res = syscall1(RB_SYS_CHDIR, reinterpret_cast<uint64_t>(path));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}


int Sysdeps<GetHostname>::operator()(char *buffer, size_t bufsize) {
    // gethostname(2) is a libc wrapper over uname(2): copy nodename,
    // NUL-terminated and truncated to bufsize. BUG-009: this used to return a
    // hardcoded "rbOS" (which did match the kernel, but bypassed the syscall
    // and was invisible to the P0 counters); go through Uname now.
    if (!buffer || bufsize == 0) return EINVAL;
    struct utsname u;
    int e = sysdep<Uname>(&u);
    if (e != 0) return e;
    size_t i = 0;
    while (i + 1 < bufsize && u.nodename[i] != '\0') {
        buffer[i] = u.nodename[i];
        i++;
    }
    buffer[i] = '\0';
    return 0;
}

int Sysdeps<Socket>::operator()(int domain, int type, int protocol, int *fd) {
    (void)domain;
    (void)type;
    (void)protocol;
    (void)fd;
    // rbOS has no network stack and no socket(2) in the kernel, so this is an
    // honest ENOSYS rather than a silent success. It exists because the linux
    // option compiles generic-helpers/netlink.hpp (via options/posix net-if.cpp),
    // whose non-template NetlinkHelper::send_request instantiates sysdep<Socket>
    // even though SysdepTraits::usesRtNetlink is false, so if_nameindex() takes
    // the ENOSYS sysdep path and never calls this. P3.6/BUG-008.
    return ENOSYS;
}

int Sysdeps<IfNameindex>::operator()(struct if_nameindex **out) {
    (void)out;
    return ENOSYS;
}

int Sysdeps<IfNametoindex>::operator()(const char *name, unsigned int *ret) {
    (void)name;
    (void)ret;
    return ENOSYS;
}

int Sysdeps<IfIndextoname>::operator()(unsigned int index, char *name) {
    (void)index;
    (void)name;
    return ENOSYS;
}

void Sysdeps<IfFreeNameindex>::operator()(struct if_nameindex *ifn) {
    (void)ifn;
}

int Sysdeps<GetEntropy>::operator()(void *buffer, size_t length) {
    // getrandom(2) (318). mlibc routes both getentropy() and getrandom() here;
    // the wrapper has already validated the caller's flags, which the kernel
    // call below does not forward (rbOS has no entropy-pool distinction), so
    // flags = 0 is the correct request.
    int64_t res = syscall3(RB_SYS_GETRANDOM, reinterpret_cast<uint64_t>(buffer),
                           static_cast<uint64_t>(length), 0);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// P3.6: the linux option ships public prctl()/statx() wrappers in
// options/linux/generic that call these sysdeps (the pre-P3.6 rbos sysdeps
// only had a directly defined prctl and no statx), so both must exist for the
// mlibc wrappers to reach the kernel.
int Sysdeps<Prctl>::operator()(int op, va_list ap, int *out) {
    unsigned long x[4];
    for (int i = 0; i < 4; i++)
        x[i] = va_arg(ap, unsigned long);

    int64_t res = syscall5(RB_SYS_PRCTL, static_cast<uint64_t>(op),
                           x[0], x[1], x[2], x[3]);
    if (res < 0) return static_cast<int>(-res);
    *out = static_cast<int>(res);
    return 0;
}

int Sysdeps<Statx>::operator()(int dirfd, const char *pathname, int flags,
                               unsigned int mask, struct statx *statxbuf) {
    // statx(2) (332). The kernel fills the UAPI `struct statx` in place; this
    // sysdep only needs the opaque pointer, so no full definition is required
    // here (abi-bits/statx.h provides it to callers/tests).
    int64_t res = syscall5(
        RB_SYS_STATX, static_cast<uint64_t>(static_cast<int64_t>(dirfd)),
        reinterpret_cast<uint64_t>(pathname),
        static_cast<uint64_t>(static_cast<int64_t>(flags)),
        static_cast<uint64_t>(mask), reinterpret_cast<uint64_t>(statxbuf));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Statfs>::operator()(const char *path, struct statfs *buf) {
    int64_t res = syscall2(RB_SYS_STATFS /* 137 */,
                           reinterpret_cast<uint64_t>(path),
                           reinterpret_cast<uint64_t>(buf));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Fstatfs>::operator()(int fd, struct statfs *buf) {
    int64_t res = syscall2(RB_SYS_FSTATFS /* 138 */,
                           static_cast<uint64_t>(fd),
                           reinterpret_cast<uint64_t>(buf));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<GetAffinity>::operator()(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
    // sched_getaffinity(2) (204). The kernel fills the mask with all online
    // CPUs and returns 0; cpusetsize validation happens kernel-side.
    int64_t res = syscall3(RB_SYS_SCHED_GETAFFINITY /* 204 */,
                           static_cast<uint64_t>(static_cast<int64_t>(pid)),
                           static_cast<uint64_t>(cpusetsize),
                           reinterpret_cast<uint64_t>(mask));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Capget>::operator()(cap_user_header_t hdrp, cap_user_data_t datap) {
    // capget(2) (125). The kernel reports the full set (no capability model),
    // which satisfies libcap/htop probes.
    int64_t res = syscall2(RB_SYS_CAPGET /* 125 */,
                           reinterpret_cast<uint64_t>(hdrp),
                           reinterpret_cast<uint64_t>(datap));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Capset>::operator()(cap_user_header_t hdrp, const cap_user_data_t datap) {
    // capset(2) (126). No capability state exists kernel-side; the call is
    // accepted as a no-op (htop drops caps at startup and aborts on ENOSYS).
    int64_t res = syscall2(RB_SYS_CAPSET /* 126 */,
                           reinterpret_cast<uint64_t>(hdrp),
                           reinterpret_cast<uint64_t>(datap));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Poll>::operator()(struct pollfd *fds, nfds_t count, int timeout, int *num_events) {
    // poll(2) (7). Bad fds surface as POLLNVAL in revents, never as errors.
    int64_t res = syscall3(RB_SYS_POLL /* 7 */,
                           reinterpret_cast<uint64_t>(fds),
                           static_cast<uint64_t>(count),
                           static_cast<uint64_t>(static_cast<int64_t>(timeout)));
    if (res < 0) return static_cast<int>(-res);
    *num_events = static_cast<int>(res);
    return 0;
}

int Sysdeps<Ppoll>::operator()(struct pollfd *fds, nfds_t count, const struct timespec *ts,
                               const sigset_t *mask, int *num_events) {
    // ppoll(2) (271): the 5th arg is the sigset size in bytes (8 here).
    int64_t res = syscall5(RB_SYS_PPOLL /* 271 */,
                           reinterpret_cast<uint64_t>(fds),
                           static_cast<uint64_t>(count),
                           reinterpret_cast<uint64_t>(ts),
                           reinterpret_cast<uint64_t>(mask),
                           static_cast<uint64_t>(NSIG / 8));
    if (res < 0) return static_cast<int>(-res);
    *num_events = static_cast<int>(res);
    return 0;
}

int Sysdeps<Pselect>::operator()(int num_fds, fd_set *read_set, fd_set *write_set,
                                fd_set *except_set, const struct timespec *timeout,
                                const sigset_t *sigmask, int *num_events) {
    // pselect6(2) (270): the 6th arg is a {sigmask_ptr, sigsetsize} record
    // (upstream mlibc uses the same layout for Linux).
    struct {
        const sigset_t *ss;
        size_t ss_len;
    } data = { sigmask, NSIG / 8 };
    int64_t res = syscall6(RB_SYS_PSELECT6 /* 270 */,
                           static_cast<uint64_t>(num_fds),
                           reinterpret_cast<uint64_t>(read_set),
                           reinterpret_cast<uint64_t>(write_set),
                           reinterpret_cast<uint64_t>(except_set),
                           reinterpret_cast<uint64_t>(timeout),
                           reinterpret_cast<uint64_t>(&data));
    if (res < 0) return static_cast<int>(-res);
    *num_events = static_cast<int>(res);
    return 0;
}

int Sysdeps<EpollCreate>::operator()(int flags, int *fd) {
    // epoll_create1(2) (291): flags is 0 or EPOLL_CLOEXEC; validation is
    // kernel-side.
    int64_t res = syscall1(RB_SYS_EPOLL_CREATE1 /* 291 */,
                           static_cast<uint64_t>(flags));
    if (res < 0) return static_cast<int>(-res);
    *fd = static_cast<int>(res);
    return 0;
}

int Sysdeps<EpollCtl>::operator()(int epfd, int mode, int fd, struct epoll_event *ev) {
    // epoll_ctl(2) (233).
    int64_t res = syscall4(RB_SYS_EPOLL_CTL /* 233 */,
                           static_cast<uint64_t>(epfd),
                           static_cast<uint64_t>(mode),
                           static_cast<uint64_t>(fd),
                           reinterpret_cast<uint64_t>(ev));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<EpollPwait>::operator()(int epfd, struct epoll_event *ev, int n, int timeout,
                                   const sigset_t *sigmask, int *raised) {
    // epoll_pwait(2) (281): the 6th arg is the sigset size in bytes (8 here).
    int64_t res = syscall6(RB_SYS_EPOLL_PWAIT /* 281 */,
                           static_cast<uint64_t>(epfd),
                           reinterpret_cast<uint64_t>(ev),
                           static_cast<uint64_t>(n),
                           static_cast<uint64_t>(static_cast<int64_t>(timeout)),
                           reinterpret_cast<uint64_t>(sigmask),
                           static_cast<uint64_t>(NSIG / 8));
    if (res < 0) return static_cast<int>(-res);
    *raised = static_cast<int>(res);
    return 0;
}

int Sysdeps<GetPriority>::operator()(int which, id_t who, int *value) {
    // getpriority(2) (140): the kernel returns 20 - nice (1..40, Linux
    // encoding); convert back to the nice value for the caller.
    int64_t res = syscall2(RB_SYS_GETPRIORITY /* 140 */,
                           static_cast<uint64_t>(which),
                           static_cast<uint64_t>(who));
    if (res < 0) return static_cast<int>(-res);
    *value = static_cast<int>(20 - res);
    return 0;
}

int Sysdeps<SetPriority>::operator()(int which, id_t who, int prio) {
    // setpriority(2) (141): prio is the nice value (-20..19); validated
    // kernel-side.
    int64_t res = syscall3(RB_SYS_SETPRIORITY /* 141 */,
                           static_cast<uint64_t>(which),
                           static_cast<uint64_t>(who),
                           static_cast<uint64_t>(static_cast<int64_t>(prio)));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Access>::operator()(const char *path, int mode) {
    // BUG-009: honour `mode` (F_OK/R_OK/W_OK/X_OK) through access(2). It used
    // to just Stat() the path, so the mode was silently ignored (W_OK on a
    // read-only file "succeeded") and the syscall never appeared in the P0
    // counters.
    int64_t res = syscall2(RB_SYS_ACCESS /* 21 */,
                           reinterpret_cast<uint64_t>(path),
                           static_cast<uint64_t>(static_cast<int64_t>(mode)));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Faccessat>::operator()(int dirfd, const char *pathname, int mode, int flags) {
    // RB_SYS_FACCESSAT (269): access(2) relative to dirfd. The kernel
    // ignores flags (no AT_EACCESS/AT_SYMLINK_NOFOLLOW semantics yet).
    // tcc calls this at startup (include/lib path probing); without the
    // sysdep every guest cc invocation died in sysdep_or_enosys.
    int64_t res = syscall4(RB_SYS_FACCESSAT,
                           static_cast<uint64_t>(static_cast<int64_t>(dirfd)),
                           reinterpret_cast<uint64_t>(pathname),
                           static_cast<uint64_t>(static_cast<int64_t>(mode)),
                           static_cast<uint64_t>(flags));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Fcntl>::operator()(int fd, int command, va_list args, int *result) {
    // Only extract the variadic argument for commands that have one;
    // reading va_arg for parameterless commands is undefined behavior.
    int arg = 0;
    switch (command) {
    case F_DUPFD:          // 0
    case F_DUPFD_CLOEXEC:  // 1030
    case F_SETFD:          // 2  (FD_CLOEXEC; BUG-014: was missing, so the
                           //     kernel always saw arg=0 and F_SETFD no-op'd)
    case F_SETFL:          // 4
    case F_SETOWN:         // 8
        arg = va_arg(args, int);
        break;
    default:
        break;
    }

    int64_t res = syscall3(RB_SYS_FCNTL,
                           static_cast<uint64_t>(fd),
                           static_cast<uint64_t>(command),
                           static_cast<uint64_t>(arg));
    if (res < 0) return static_cast<int>(-res);
    *result = static_cast<int>(res);
    return 0;
}

// P3.1: public prctl(2). mlibc ships it in options/linux/generic/sys-prctl.cpp
// and its `Prctl` sysdep (mlibc/options/internal/include/mlibc/sysdep-signatures.hpp)
// is declared only under __MLIBC_LINUX_OPTION, which the rbOS sysdeps do not
// enable — so neither the symbol nor the sysdep exists here even though
// <sys/prctl.h> is installed. Provide the public function directly; a future
// sysdeps config that enables the Linux option compiles this out and uses the
// upstream implementation instead. Not built for the rtld (generic.cpp is
// shared with ld.so, where `errno`/TLS are unavailable and unused).
#if !__MLIBC_LINUX_OPTION && !MLIBC_BUILDING_RTLD
extern "C" int prctl(int op, ...) {
    va_list ap;
    va_start(ap, op);
    unsigned long x[4];
    for (int i = 0; i < 4; i++)
        x[i] = va_arg(ap, unsigned long);
    va_end(ap);

    int64_t res = syscall5(RB_SYS_PRCTL, static_cast<uint64_t>(op),
                           x[0], x[1], x[2], x[3]);
    if (res < 0) {
        errno = static_cast<int>(-res);
        return -1;
    }
    return static_cast<int>(res);
}
#endif

// User/Group IDs (Stubs for Security Checks)
int Sysdeps<GetPid>::operator()() {
    int64_t res = syscall0(RB_SYS_GETPID);
    return (res < 0) ? 1 : static_cast<pid_t>(res);
}

pid_t Sysdeps<GetPpid>::operator()() {
    int64_t res = syscall0(RB_SYS_GETPPID);
    return (res < 0) ? 1 : static_cast<pid_t>(res);
}

int Sysdeps<GetPgid>::operator()(pid_t pid, pid_t *pgid) {
    int64_t res = syscall1(RB_SYS_GETPGID /* 121 */,
                           static_cast<uint64_t>(static_cast<int64_t>(pid)));
    if (res < 0) return static_cast<int>(-res);
    *pgid = static_cast<pid_t>(res);
    return 0;
}

uid_t Sysdeps<GetUid>::operator()() {
    int64_t res = syscall0(RB_SYS_GETUID);
    return (res < 0) ? 0 : static_cast<uid_t>(res);
}

uid_t Sysdeps<GetEuid>::operator()() {
    int64_t res = syscall0(RB_SYS_GETEUID);
    return (res < 0) ? 0 : static_cast<uid_t>(res);
}

gid_t Sysdeps<GetGid>::operator()() {
    int64_t res = syscall0(RB_SYS_GETGID);
    return (res < 0) ? 0 : static_cast<gid_t>(res);
}

gid_t Sysdeps<GetEgid>::operator()() {
    int64_t res = syscall0(RB_SYS_GETEGID);
    return (res < 0) ? 0 : static_cast<gid_t>(res);
}

int Sysdeps<SetPgid>::operator()(pid_t pid, pid_t pgid) {
    int64_t res = syscall2(RB_SYS_SETPGID, pid, pgid);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<SetSid>::operator()(pid_t *sid) {
    int64_t res = syscall0(RB_SYS_SETSID);
    if (res < 0) return static_cast<int>(-res);
    *sid = static_cast<pid_t>(res);
    return 0;
}

int Sysdeps<Waitpid>::operator()(pid_t pid, int *status, int flags, struct rusage *ru, pid_t *ret_pid) {
    (void)ru;

    int64_t res = syscall3(RB_SYS_WAITPID, pid, reinterpret_cast<uint64_t>(status), flags);
    if (res < 0) {
        return -res;
    }
    if (ret_pid) {
        *ret_pid = static_cast<pid_t>(res);
    }
    return 0;
}

int Sysdeps<GetRusage>::operator()(int scope, struct rusage *usage) {
    // BUG-009: route through getrusage(2) instead of zeroing locally, so the
    // call reaches the kernel and shows up in the P0 counters. The kernel
    // still has no accounting and zeroes the struct, so the result is
    // unchanged — but the syscall is now real (and NULL/short buffers EFAULT).
    int64_t res = syscall2(RB_SYS_GETRUSAGE /* 98 */,
                           static_cast<uint64_t>(static_cast<int64_t>(scope)),
                           reinterpret_cast<uint64_t>(usage));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// Signals
int Sysdeps<Sigprocmask>::operator()(int how, const sigset_t *set, sigset_t *oldset) {
    int64_t res = syscall4(RB_SYS_SIGPROCMASK, how,
                           reinterpret_cast<uint64_t>(set),
                           reinterpret_cast<uint64_t>(oldset), 8);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Sigaction>::operator()(int how, const struct sigaction *__restrict action,
                                   struct sigaction *__restrict old_action) {
    // Install our restorer trampoline unless the caller supplied one: the
    // kernel places it as the handler's return address so execution comes
    // back through RB_SYS_SIGRETURN (see handle_pending_signals).
    struct sigaction patched;
    if (action && !(action->sa_flags & SA_RESTORER)) {
        memcpy(&patched, action, sizeof(struct sigaction));
        patched.sa_flags |= SA_RESTORER;
        patched.sa_restorer = &rbos_sigrestore;
        action = &patched;
    }
    int64_t res = syscall4(RB_SYS_SIGACTION, how,
                           reinterpret_cast<uint64_t>(action),
                           reinterpret_cast<uint64_t>(old_action), 8);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Sigsuspend>::operator()(const sigset_t *set) {
    int64_t res = syscall2(RB_SYS_SIGSUSPEND, reinterpret_cast<uint64_t>(set), 8);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}


int Sysdeps<VmUnmap>::operator()(void *pointer, size_t size) {
    int64_t result = syscall2(RB_SYS_MUNMAP, reinterpret_cast<uint64_t>(pointer), size);
    if (result < 0) {
        return static_cast<int>(-result);
    }
    return 0;
}

int Sysdeps<VmProtect>::operator()(void *pointer, size_t size, int prot) {
    int64_t result = syscall3(RB_SYS_MPROTECT, reinterpret_cast<uint64_t>(pointer), size, prot);
    if (result < 0) {
        return static_cast<int>(-result);
    }
    return 0;
}

int Sysdeps<Execve>::operator()(const char *path, char *const argv[], char *const envp[]) {
    int64_t res = syscall3(RB_SYS_EXECVE,
                           reinterpret_cast<uint64_t>(path),
                           reinterpret_cast<uint64_t>(argv),
                           reinterpret_cast<uint64_t>(envp));
    if (res < 0) {
        return static_cast<int>(-res);
    }
    return ENOSYS;
}



int Sysdeps<Unlinkat>::operator()(int dirfd, const char *path, int flags) {
    // dirfd-relative resolution needs procfs; AT_FDCWD/absolute work now.
    int64_t res = syscall3(RB_SYS_UNLINKAT /* 263 */,
                           static_cast<uint64_t>(static_cast<int64_t>(dirfd)),
                           reinterpret_cast<uint64_t>(path),
                           static_cast<uint64_t>(flags));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Mkdir>::operator()(const char *path, mode_t mode) {
    int64_t res = syscall2(RB_SYS_MKDIR /* 83 */,
                           reinterpret_cast<uint64_t>(path),
                           static_cast<uint64_t>(mode));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Mkdirat>::operator()(int dirfd, const char *path, mode_t mode) {
    (void)dirfd; // same limitation as Openat
    int64_t res = syscall2(RB_SYS_MKDIR /* 83 */,
                           reinterpret_cast<uint64_t>(path),
                           static_cast<uint64_t>(mode));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Rmdir>::operator()(const char *path) {
    int64_t res = syscall1(RB_SYS_RMDIR /* 84 */, reinterpret_cast<uint64_t>(path));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

} // namespace mlibc


// ===================== phase-1 ABI =====================

namespace mlibc {

int Sysdeps<Pipe>::operator()(int *fds, int flags) {
    int64_t res = syscall2(RB_SYS_PIPE2,
                           reinterpret_cast<uint64_t>(fds),
                           static_cast<uint64_t>(flags));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Readv>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_read) {
    int64_t res = syscall3(RB_SYS_READV,
                           static_cast<uint64_t>(fd),
                           reinterpret_cast<uint64_t>(iovs),
                           static_cast<uint64_t>(iovc));
    if (res < 0) return static_cast<int>(-res);
    *bytes_read = static_cast<ssize_t>(res);
    return 0;
}

int Sysdeps<Writev>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_written) {
    int64_t res = syscall3(RB_SYS_WRITEV,
                           static_cast<uint64_t>(fd),
                           reinterpret_cast<uint64_t>(iovs),
                           static_cast<uint64_t>(iovc));
    if (res < 0) return static_cast<int>(-res);
    *bytes_written = static_cast<ssize_t>(res);
    return 0;
}

int Sysdeps<Dup2>::operator()(int fd, int flags, int newfd) {
    uint64_t number = (flags & O_CLOEXEC) ? RB_SYS_DUP3 : RB_SYS_DUP2;
    int64_t res = syscall3(number,
                           static_cast<uint64_t>(fd),
                           static_cast<uint64_t>(newfd),
                           static_cast<uint64_t>(flags));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// dup(2): duplicate to the lowest free fd, honoring O_CLOEXEC.
int Sysdeps<Dup>::operator()(int fd, int flags, int *newfd) {
    int64_t cmd = (flags & O_CLOEXEC) ? 1030 /* F_DUPFD_CLOEXEC */ : 0 /* F_DUPFD */;
    int64_t res = syscall3(RB_SYS_FCNTL /* 72 */,
                           static_cast<uint64_t>(fd),
                           static_cast<uint64_t>(cmd),
                           0);
    if (res < 0) return static_cast<int>(-res);
    if (newfd) *newfd = static_cast<int>(res);
    return 0;
}

// opendir(3): the handle is a plain directory fd.
int Sysdeps<OpenDir>::operator()(const char *path, int *handle) {
    int64_t res = syscall4(RB_SYS_OPENAT /* 257 */,
                           static_cast<uint64_t>(-100) /* AT_FDCWD */,
                           reinterpret_cast<uint64_t>(path),
                           00 /* O_RDONLY */ | 0200000 /* O_DIRECTORY */,
                           0);
    if (res < 0) return static_cast<int>(-res);
    if (handle) *handle = static_cast<int>(res);
    return 0;
}

// readdir(3): kernel getdents64 fills linux_dirent64 records directly.
int Sysdeps<ReadEntries>::operator()(int handle, void *buffer, size_t max_size, size_t *bytes_read) {
    int64_t res = syscall3(RB_SYS_GETDENTS64 /* 217 */,
                           static_cast<uint64_t>(handle),
                           reinterpret_cast<uint64_t>(buffer),
                           static_cast<uint64_t>(max_size));
    if (res < 0) return static_cast<int>(-res);
    if (bytes_read) *bytes_read = static_cast<size_t>(res);
    return 0;
}

int Sysdeps<Openat>::operator()(int dirfd, const char *path, int flags, mode_t mode, int *fd) {
    (void)mode;
    // Kernel handles absolute paths regardless of dirfd; dirfd-relative
    // lookups need procfs and return ENOTDIR there.
    int64_t res = syscall4(RB_SYS_OPENAT /* 257 */,
                           static_cast<uint64_t>(dirfd),
                           reinterpret_cast<uint64_t>(path),
                           static_cast<uint64_t>(flags),
                           static_cast<uint64_t>(mode));
    if (res < 0) return static_cast<int>(-res);
    *fd = static_cast<int>(res);
    return 0;
}

int Sysdeps<Umask>::operator()(mode_t mode, mode_t *old) {
    int64_t res = syscall1(RB_SYS_UMASK, static_cast<uint64_t>(mode));
    if (res < 0) return static_cast<int>(-res);
    *old = static_cast<mode_t>(res);
    return 0;
}

int Sysdeps<Utimensat>::operator()(int dirfd, const char *pathname,
                                   const struct timespec times[2], int flags) {
    int64_t res = syscall4(RB_SYS_UTIMENSAT /* 280 */,
                           static_cast<uint64_t>(dirfd),
                           reinterpret_cast<uint64_t>(pathname),
                           reinterpret_cast<uint64_t>(times),
                           static_cast<uint64_t>(flags));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Chmod>::operator()(const char *pathname, mode_t mode) {
    int64_t res = syscall2(RB_SYS_CHMOD /* 90 */,
                           reinterpret_cast<uint64_t>(pathname),
                           static_cast<uint64_t>(mode));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Fchmod>::operator()(int fd, mode_t mode) {
    int64_t res = syscall2(RB_SYS_FCHMOD /* 91 */,
                           static_cast<uint64_t>(fd),
                           static_cast<uint64_t>(mode));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Fchmodat>::operator()(int dirfd, const char *pathname,
                                  mode_t mode, int flags) {
    (void)dirfd; (void)flags; // pathname only, absolute paths supported
    int64_t res = syscall2(RB_SYS_CHMOD /* 90 */,
                           reinterpret_cast<uint64_t>(pathname),
                           static_cast<uint64_t>(mode));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Fchownat>::operator()(int dirfd, const char *pathname,
                                  uid_t owner, gid_t group, int flags) {
    // fchown(fd, ...) comes through here as Fchownat(fd, "", ...) with
    // AT_EMPTY_PATH (0x1000); route it to the fchown syscall.
    if (flags & 0x1000 /* AT_EMPTY_PATH */ && (pathname == nullptr || pathname[0] == '\0')) {
        int64_t res = syscall3(RB_SYS_FCHOWN /* 93 */,
                               static_cast<uint64_t>(dirfd),
                               static_cast<uint64_t>(static_cast<uid_t>(owner)),
                               static_cast<uint64_t>(static_cast<gid_t>(group)));
        if (res < 0) return static_cast<int>(-res);
        return 0;
    }
    (void)dirfd; (void)flags; // pathname only
    int64_t res = syscall3(RB_SYS_CHOWN /* 92 */,
                           reinterpret_cast<uint64_t>(pathname),
                           static_cast<uint64_t>(static_cast<uid_t>(owner)),
                           static_cast<uint64_t>(static_cast<gid_t>(group)));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<SetUid>::operator()(uid_t uid) {
    int64_t res = syscall1(RB_SYS_SETUID /* 105 */, static_cast<uint64_t>(uid));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<SetGid>::operator()(gid_t gid) {
    int64_t res = syscall1(RB_SYS_SETGID /* 106 */, static_cast<uint64_t>(gid));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<SetReuid>::operator()(uid_t ruid, uid_t euid) {
    int64_t res = syscall2(RB_SYS_SETREUID /* 113 */,
                           static_cast<uint64_t>(static_cast<uid_t>(ruid)),
                           static_cast<uint64_t>(static_cast<uid_t>(euid)));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<SetRegid>::operator()(gid_t rgid, gid_t egid) {
    int64_t res = syscall2(RB_SYS_SETREGID /* 114 */,
                           static_cast<uint64_t>(static_cast<gid_t>(rgid)),
                           static_cast<uint64_t>(static_cast<gid_t>(egid)));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// ---- resource limits (P1: per-process RLIMIT_STACK / RLIMIT_NOFILE,
// fork inherits, exec preserves; RLIM_INFINITY for everything else;
// soft RLIMIT_NOFILE is enforced as EMFILE on open/pipe/dup) ----
int Sysdeps<GetRlimit>::operator()(int resource, struct rlimit *limit) {
    int64_t res = syscall2(RB_SYS_GETRLIMIT /* 97 */,
                           static_cast<uint64_t>(resource),
                           reinterpret_cast<uint64_t>(limit));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<SetRlimit>::operator()(int resource, const struct rlimit *limit) {
    // prlimit64(0, resource, new_limit, NULL): stores the new limit in the
    // current process (cur<=max, unprivileged hard raise fails EPERM).
    int64_t res = syscall4(RB_SYS_PRLIMIT64 /* 302 */,
                           0 /* pid 0 = self */,
                           static_cast<uint64_t>(resource),
                           reinterpret_cast<uint64_t>(limit),
                           0 /* old_limit = NULL */);
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

// ---- rename / renameat ----
//
// POSIX rename(2) is dispatched through the kernel's RB_SYS_RENAME (Linux
// x86-64 __NR_rename = 82). renameat(2) is the same syscall with the
// caller's dirfds: rbOS resolves absolute paths regardless of dirfd, so a
// relative path with a real dirfd is rejected by at_dirfd_check (ENOTDIR),
// matching openat()/newfstatat().

int Sysdeps<Rename>::operator()(const char *path, const char *new_path) {
    int64_t res = syscall2(RB_SYS_RENAME /* 82 */,
                           reinterpret_cast<uint64_t>(path),
                           reinterpret_cast<uint64_t>(new_path));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}

int Sysdeps<Renameat>::operator()(int olddirfd, const char *old_path,
                                  int newdirfd, const char *new_path) {
    // rbOS has no /proc/self/fd to resolve a dirfd-relative path, so it
    // delegates to the kernel which rejects relative paths with a real
    // dirfd via at_dirfd_check (ENOTDIR). Absolute paths work regardless
    // of the dirfd value.
    int64_t res = syscall4(RB_SYS_RENAME /* 82 */,
                           static_cast<uint64_t>(static_cast<int64_t>(olddirfd)),
                           reinterpret_cast<uint64_t>(old_path),
                           static_cast<uint64_t>(static_cast<int64_t>(newdirfd)),
                           reinterpret_cast<uint64_t>(new_path));
    if (res < 0) return static_cast<int>(-res);
    return 0;
}


} // namespace mlibc
