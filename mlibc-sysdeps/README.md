# mlibc Port for rbOS

This directory contains the **sysdeps** (system dependencies) implementation for porting mlibc to rbOS, a custom 64-bit x86_64 operating system.

## Overview

mlibc is a portable C standard library that uses a "sysdeps" architecture for OS-specific implementations. This port provides the necessary glue code between mlibc and the rbOS kernel.

## Architecture

### Directory Structure

```
sysdeps/rbos/
├── crt-x86_64/
│   └── crt0.S                 # C runtime startup code
├── include/
│   ├── abi-bits/              # ABI-specific headers
│   │   ├── abi.h              # ABI constants
│   │   ├── access.h           # File access constants
│   │   ├── auxv.h             # Auxiliary vector types
│   │   ├── errno.h            # Error codes
│   │   ├── fcntl.h            # File control operations
│   │   ├── in.h               # Internet address family
│   │   ├── ioctls.h           # ioctl definitions
│   │   ├── mode_t.h           # File mode type
│   │   ├── pid_t.h            # Process ID type
│   │   ├── seek-whence.h      # lseek() constants
│   │   ├── signal.h           # Signal definitions
│   │   ├── socket.h           # Socket definitions
│   │   ├── stat.h             # File status structure
│   │   ├── termios.h          # Terminal I/O
│   │   ├── vm-flags.h         # Memory mapping flags
│   │   └── wait.h             # Process wait macros
│   └── rbos/                  # rbOS-specific headers
├── generic.cpp                # Sysdeps implementation
└── meson.build                # Build configuration
```

## rbOS Syscall Interface

The port uses the following rbOS syscalls via x86_64 `syscall` instruction:

| Number | Name       | Description                    |
|--------|------------|--------------------------------|
| 0      | SYS_READ   | Read from file descriptor      |
| 1      | SYS_WRITE  | Write to file descriptor       |
| 2      | SYS_EXIT   | Terminate process              |
| 3      | SYS_OPEN   | Open file                      |
| 4      | SYS_CLOSE  | Close file descriptor          |
| 5      | SYS_SBRK   | Adjust heap size               |
| 6      | SYS_LSEEK  | Seek in file                   |
| 7      | SYS_MMAP   | Map anonymous memory           |
| 8      | SYS_MUNMAP | Unmap memory                   |

### x86_64 Syscall Convention

Arguments are passed in registers:
- **rax**: Syscall number
- **rdi**: arg1
- **rsi**: arg2
- **rdx**: arg3
- **r10**: arg4
- **r8**: arg5
- **r9**: arg6

Return value in **rax** (negative values indicate errors).

## Implemented mlibc Functions

### Core Functions (Required by mlibc)

- `mlibc::sys_libc_log()` - Debug logging to serial/VGA
- `mlibc::sys_libc_panic()` - Panic handler
- `mlibc::sys_exit()` - Process termination

### Memory Management

- `mlibc::sys_anon_allocate()` - Anonymous memory allocation (malloc backing)
- `mlibc::sys_anon_free()` - Free anonymous memory
- `mlibc::sys_vm_map()` - Memory mapping (mmap)
- `mlibc::sys_vm_unmap()` - Memory unmapping (munmap)

### File Operations

- `mlibc::sys_open()` - Open file
- `mlibc::sys_close()` - Close file
- `mlibc::sys_read()` - Read from file
- `mlibc::sys_write()` - Write to file
- `mlibc::sys_seek()` - Seek in file

### Process Information

- `mlibc::sys_getpid()` - Get process ID
- `mlibc::sys_getppid()` - Get parent process ID

### Time Functions

- `mlibc::sys_clock_get()` - Get current time (stub)

## Integration with mlibc

### Step 1: Clone mlibc Repository

```bash
cd /home/rbashkort/AAPP/rbOS
git clone https://github.com/managarm/mlibc.git
cd mlibc
```

### Step 2: Copy Sysdeps

```bash
cp -r ../mlibc-sysdeps/sysdeps/rbos sysdeps/
```

### Step 3: Configure Build

Create a cross-compilation file `rbos-cross.txt`:

```ini
[binaries]
c = 'x86_64-elf-gcc'
cpp = 'x86_64-elf-g++'
ar = 'x86_64-elf-ar'
strip = 'x86_64-elf-strip'

[host_machine]
system = 'rbos'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
c_args = ['-mcmodel=kernel', '-fno-pie', '-mno-red-zone', '-fno-stack-protector']
cpp_args = ['-mcmodel=kernel', '-fno-pie', '-mno-red-zone', '-fno-stack-protector', '-fno-exceptions', '-fno-rtti']
```

### Step 4: Build mlibc

```bash
# Create build directory
mkdir build-rbos
cd build-rbos

# Configure with meson
meson setup \
    --cross-file ../rbos-cross.txt \
    --prefix=/opt/rbos-sysroot \
    -Dheaders_only=false \
    -Dbuild_tests=false \
    -Ddisable_crypt_option=true \
    -Ddisable_iconv_option=true \
    -Ddisable_intl_option=true \
    ..

# Build
ninja

# Install to sysroot
ninja install
```

### Step 5: Link with rbOS Programs

When compiling programs for rbOS, link against mlibc:

```bash
x86_64-elf-gcc \
    -mcmodel=kernel \
    -fno-pie \
    -mno-red-zone \
    -nostdlib \
    -o hello \
    /opt/rbos-sysroot/lib/crt0.o \
    hello.c \
    -L/opt/rbos-sysroot/lib \
    -lc \
    -I/opt/rbos-sysroot/include
```

## Example Program

```c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    printf("Hello from rbOS with mlibc!\n");
    printf("Arguments: %d\n", argc);
    
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    
    return 0;
}
```

## Kernel Integration

The rbOS kernel must be updated to handle these syscalls. Key changes:

1. **Extended syscall table** in `kernel/arch/x86_64/syscall.h`:
   - Added SYS_LSEEK, SYS_MMAP, SYS_MUNMAP

2. **File descriptor table** in `kernel/arch/x86_64/syscall.c`:
   - Simple fd table (MAX_FDS = 256)
   - Track offsets for each fd
   - stdin/stdout/stderr reserved (0, 1, 2)

3. **Memory management**:
   - sys_sbrk: Heap allocation via VMM
   - sys_mmap: Anonymous page mapping
   - sys_munmap: Page unmapping and deallocation

4. **VFS integration**:
   - sys_open: Routes to vfs_open()
   - sys_read: Routes to vfs_read() with offset tracking
   - sys_write: Routes to vfs_write() with offset tracking
   - sys_close: Routes to vfs_close()
   - sys_lseek: Implements SEEK_SET/CUR/END

## Testing

### Unit Tests

Test basic C library functionality:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

void test_malloc() {
    void *ptr = malloc(1024);
    if (ptr) {
        printf("malloc: OK\n");
        free(ptr);
    }
}

void test_file_io() {
    int fd = open("/test.txt", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf));
        printf("read %ld bytes\n", n);
        close(fd);
    }
}

int main() {
    test_malloc();
    test_file_io();
    return 0;
}
```

## Building the rbOS mlibc (linux option)

The rbOS port is built by the top-level helper scripts, **not** by hand:

```bash
make mlibc              # static libc.a + crt  (tools/build-mlibc-static.sh)
make mlibc-dynamic      # libc.so + ld.so      (tools/build-mlibc-dynamic.sh)
make populate-sysroot   # install libs + headers into sysroot/
```

Both scripts configure mlibc with `-Dposix_option=enabled`,
`-Dlinux_option=enabled`, `-Dglibc_option=enabled` and `-Dbsd_option=enabled`
(P3.6). The linux option is what provides the Linux API set
(`prctl()`, `statx()`, `<sys/prctl.h>`, xattr/inotify/eventfd/…); the glibc and
bsd options are required because mlibc 7.0.0 does not build with `linux=on`
alone (the `versionsort64` alias needs glibc's `versionsort`), and they in turn
supply `<sys/ioctl.h>`/`<utmp.h>`.

### linux_kernel_headers dependency

meson requires `-Dlinux_kernel_headers=<dir>` when the linux option is enabled
and rejects a system include path (`meson.build` compares the path against the
compiler's search list), so the UAPI headers must be staged into a dedicated
directory first:

```bash
tools/stage-linux-headers.sh           # copies linux/, asm/, asm-generic/
                                       # from /usr/include into build/linux-headers
```

Override the source with `LINUX_HEADERS_SRC` (e.g. a kernel tree's
`usr/include` after `make headers_install`) and the destination with
`LINUX_HEADERS_DST`. Both `tools/build-mlibc-*.sh` call this script
automatically, so `make mlibc`/`make libs`/`make world` are self-contained on a
host that has `/usr/include/{linux,asm,asm-generic}`.

`tools/populate-sysroot.sh` also installs the staged UAPI headers into
`sysroot/usr/include/`: with the linux option on, installed public headers such
as `<sys/ioctl.h>` and `<linux/*>` pull in `<asm/*>` at user-compile time, so
the sysroot needs them too.

## Known Limitations

1. **Single process**: No fork/exec support yet
2. **No signals**: Signal handling not implemented
3. **No networking**: Socket operations stubbed
4. **No threading**: pthread not supported
5. **Stub time**: Clock functions return 0

## Future Enhancements

- [ ] Implement fork/exec/wait syscalls
- [ ] Add signal handling support
- [ ] Implement TCP/IP stack for networking
- [ ] Add pthread support with kernel threads
- [ ] Implement real-time clock tracking
- [ ] Add directory operations (readdir, mkdir, etc.)
- [ ] Implement stat/fstat syscalls
- [ ] Add pipe and IPC support

## Debugging

Enable verbose logging in generic.cpp:

```cpp
void sys_libc_log(const char *message) {
    // Writes to COM1 (visible in QEMU with -serial stdio)
    size_t len = 0;
    while (message[len]) len++;
    syscall3(SYS_WRITE, 1, reinterpret_cast<uint64_t>(message), len);
}
```

Run QEMU with serial output:

```bash
qemu-system-x86_64 -cdrom rbos.iso -serial stdio -no-reboot -no-shutdown
```

## Contributing

When extending the port:

1. Add new syscalls to both kernel and sysdeps
2. Update syscall number mappings
3. Implement corresponding mlibc::sys_* functions
4. Add necessary ABI headers
5. Test with real programs

## License

This sysdeps implementation follows mlibc's MIT license.

## References

- [mlibc GitHub](https://github.com/managarm/mlibc)
- [mlibc Documentation](https://github.com/managarm/mlibc/tree/master/docs)
- [rbOS Project](../Plan.md)
- [x86_64 Syscall ABI](https://wiki.osdev.org/System_V_ABI)
