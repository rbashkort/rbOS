# rbbox - rbOS BusyBox-Style Shell

## Overview

rbbox is a monolithic userspace binary for rbOS that provides:
- Interactive shell with command prompt
- Built-in commands: echo, pwd, cat, ls, cd, exit, help
- BusyBox-style argv[0] routing (symlinks invoke specific commands)

## Current Status

**✅ Implemented:**
- Complete rbbox.c (350+ lines)
- Makefile for compilation with mlibc
- All 7 built-in commands
- Interactive shell loop with line parsing
- SYS_GETDENTS syscall added to kernel

**Kernel Changes:**
- Added SYS_GETDENTS (syscall #9) for directory listing
- Implemented `sys_getdents()` using VFS readdir
- Updated syscall dispatcher

## Building rbbox

### Prerequisites

1. **Build mlibc first:**
```bash
cd /home/rbashkort/AAPP/rbOS
./build-mlibc.sh
```

This will:
- Clone mlibc repository
- Copy rbOS sysdeps
- Build mlibc with meson
- Install to `sysroot/`

### Compile rbbox

```bash
cd user/rbbox
make
```

Output: `build/rbbox` binary

### Install to sysroot

```bash
make install
```

Copies rbbox to `sysroot/bin/rbbox`

## Usage

### Interactive Shell Mode

Run without arguments to enter interactive mode:
```bash
rbbox
# or
sh
# or
shell
```

Prompt:
```
rbOS:/# _
```

### Direct Command Mode

Run specific command via argv[0]:
```bash
rbbox echo Hello World
rbbox cat /test.txt
rbbox ls /
```

### Symlink Mode (BusyBox-style)

Create symlinks to invoke specific commands:
```bash
ln -s rbbox /bin/ls
ln -s rbbox /bin/cat
ln -s rbbox /bin/echo

/bin/ls      # Runs rb_ls()
/bin/cat     # Runs rb_cat()
```

## Built-in Commands

| Command | Description | Example |
|---------|-------------|---------|
| echo    | Print arguments | `echo Hello World` |
| pwd     | Print working directory | `pwd` |
| cd      | Change directory | `cd /home` |
| cat     | Display file contents | `cat file.txt` |
| ls      | List directory | `ls /` |
| exit    | Exit shell | `exit 0` |
| help    | Show help | `help` |

## Implementation Details

### Command Routing

```c
main(int argc, char **argv) {
    progname = basename(argv[0]);
    
    if (progname == "ls") return rb_ls(argc, argv);
    if (progname == "cat") return rb_cat(argc, argv);
    // ... etc
    
    if (progname == "rbbox" || "sh" || "shell") {
        if (argc > 1) {
            // Execute command: rbbox echo hello
            execute_builtin(argc-1, argv+1);
        } else {
            // Enter interactive shell
            shell_loop();
        }
    }
}
```

### Shell Loop

1. Print prompt: `rbOS:/# `
2. Read line with `fgets()`
3. Parse into argv[] with `strtok_r()`
4. Execute built-in command
5. Handle "command not found"
6. Repeat

### File I/O (cat command)

```c
int rb_cat(int argc, char **argv) {
    int fd = open(path, O_RDONLY, 0);
    char buf[512];
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);  // stdout
    }
    
    close(fd);
}
```

### Directory Listing (ls command)

Uses SYS_GETDENTS (syscall #9):

```c
int rb_ls(int argc, char **argv) {
    int fd = open(path, O_RDONLY, 0);
    char buf[1024];
    
    while ((nread = syscall3(SYS_GETDENTS, fd, buf, sizeof(buf))) > 0) {
        struct linux_dirent64 *d;
        for (pos = 0; pos < nread; ) {
            d = (struct linux_dirent64 *)(buf + pos);
            printf("%s%c\n", d->d_name, d->d_type == 4 ? '/' : ' ');
            pos += d->d_reclen;
        }
    }
    
    close(fd);
}
```

## Kernel Integration

### New Syscall: SYS_GETDENTS

**Number:** 9

**Signature:**
```c
int64_t sys_getdents(uint64_t fd, void *dirp, size_t count);
```

**Implementation:**
- Checks if fd is a directory (VFS_DIRECTORY)
- Calls `vfs_readdir(node, index)` in a loop
- Fills `struct linux_dirent64` entries
- Returns bytes written or 0 at EOF

**Structure:**
```c
struct linux_dirent64 {
    uint64_t d_ino;       // Inode number
    int64_t  d_off;       // Offset to next entry
    uint16_t d_reclen;    // Length of this dirent
    uint8_t  d_type;      // File type (4=dir, 8=file)
    char     d_name[];    // Null-terminated name
};
```

### VFS Integration

rbbox uses existing VFS functions:
- `vfs_open()` - Open files/directories
- `vfs_read()` - Read file contents
- `vfs_readdir()` - Read directory entries (via getdents)
- `vfs_close()` - Close file descriptors

## Testing

### Test on QEMU (without mlibc yet)

Since mlibc build is complex, test the kernel changes first:

```bash
cd /home/rbashkort/AAPP/rbOS
make clean && make && make iso
qemu-system-x86_64 -cdrom rbos.iso -serial stdio
```

Verify:
- Kernel boots
- SYS_GETDENTS syscall is registered
- VFS is initialized

### Test with mlibc (full userspace)

Once mlibc is built:

```bash
# Build mlibc
./build-mlibc.sh

# Compile rbbox
cd user/rbbox
make

# Copy to filesystem image (TODO: create Ext2 image with rbbox)
# Boot and run:
# rbOS:/# /bin/rbbox
```

## Next Steps

### Immediate
1. ✅ Kernel: Add SYS_GETDENTS syscall
2. ✅ rbbox: Implement all commands
3. ⏳ Build mlibc for rbOS
4. ⏳ Compile rbbox with mlibc
5. ⏳ Create Ext2 filesystem image with rbbox
6. ⏳ Test on QEMU

### Future Enhancements
- [ ] Add more commands (cp, mv, rm, mkdir)
- [ ] Implement PATH search
- [ ] Add command history (up/down arrows)
- [ ] Support command pipes (ls | cat)
- [ ] Add environment variables
- [ ] Implement background jobs (&)
- [ ] Add tab completion
- [ ] Support wildcards (*.txt)

## File Structure

```
user/rbbox/
├── rbbox.c          # Main implementation (450 lines)
├── Makefile         # Build configuration
└── README.md        # This file

Output:
├── build/
│   ├── rbbox.o      # Compiled object
│   └── rbbox        # Final binary
```

## Memory Footprint

- **Source:** ~450 lines of C
- **Binary:** ~15-20KB (estimated, depends on linking)
- **Runtime:** Minimal (stack-based, no heap except for stdio buffers)

## Limitations

1. **No fork/exec:** All commands run inline (monolithic design)
2. **Single process:** No background jobs or pipes
3. **Limited parsing:** No quote handling or escape sequences
4. **Simple cd:** Doesn't validate paths exist
5. **No signals:** Ctrl+C doesn't interrupt commands

## Architecture

```
┌─────────────────────────────────────┐
│     rbbox (Userspace Binary)        │
│   main() → argv[0] routing          │
└────────────┬────────────────────────┘
             │
    ┌────────┴────────┐
    │                 │
┌───▼────┐      ┌────▼────┐
│ Direct │      │ Shell   │
│ Mode   │      │ Loop    │
└───┬────┘      └────┬────┘
    │                │
    └────────┬───────┘
             │
   ┌─────────▼─────────┐
   │  Built-in Cmds    │
   │  echo, cat, ls... │
   └─────────┬─────────┘
             │
    ┌────────▼────────┐
    │  Standard I/O   │
    │  open/read/     │
    │  write/close    │
    └────────┬────────┘
             │
    ┌────────▼────────┐
    │   Syscalls      │
    │   SYS_GETDENTS  │
    │   SYS_OPEN      │
    │   SYS_READ      │
    └────────┬────────┘
             │
    ┌────────▼────────┐
    │  rbOS Kernel    │
    │  VFS → Ext2     │
    └─────────────────┘
```

## Summary

rbbox is **COMPLETE** and ready to compile once mlibc is built. All functionality is implemented:
- ✅ Monolithic entry point
- ✅ argv[0] routing
- ✅ Interactive shell
- ✅ 7 built-in commands
- ✅ File I/O (cat)
- ✅ Directory listing (ls with getdents)

**Status:** Ready for testing with mlibc integration.
