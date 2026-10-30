#ifndef _ABIBITS_IOCTLS_H
#define _ABIBITS_IOCTLS_H

#include <mlibc-config.h>

// struct winsize is defined once, in mlibc's canonical internal header.
// Defining it here as well clashes with <bits/winsize.h> (pulled in by
// <termios.h> and, under the linux option, by <sys/ioctl.h>): BUG-006.
#include <bits/winsize.h>

#if __MLIBC_LINUX_OPTION
// With the linux option enabled, take the ioctl encoding macros and the
// terminal ioctl numbers from the kernel UAPI headers -- the same headers
// <sys/ioctl.h> pulls in -- so the macros are defined in exactly one place.
#include <asm/ioctls.h>
#endif

// Fallback definitions for when the kernel UAPI headers are unavailable (linux
// option off). _ASM_GENERIC_IOCTL_H is the include guard of
// <asm-generic/ioctl.h>; if it is set, the macro block below was already
// provided by the kernel headers.
#ifndef _ASM_GENERIC_IOCTL_H

// ioctl direction bits
#define _IOC_NONE  0U
#define _IOC_WRITE 1U
#define _IOC_READ  2U

// ioctl macros
#define _IOC(dir,type,nr,size) \
    (((dir)  << 30) | \
     ((type) << 8) | \
     ((nr)   << 0) | \
     ((size) << 16))

#define _IO(type,nr)        _IOC(_IOC_NONE,(type),(nr),0)
#define _IOR(type,nr,size)  _IOC(_IOC_READ,(type),(nr),sizeof(size))
#define _IOW(type,nr,size)  _IOC(_IOC_WRITE,(type),(nr),sizeof(size))
#define _IOWR(type,nr,size) _IOC(_IOC_READ|_IOC_WRITE,(type),(nr),sizeof(size))

// Terminal ioctls
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414

#endif // _ASM_GENERIC_IOCTL_H

#endif // _ABIBITS_IOCTLS_H
