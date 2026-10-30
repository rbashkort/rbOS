/* rbOS port shims for e2fsck.
 *
 * __dso_handle: libgcc/libc destructor machinery expects it (glibc puts
 * it in rcrt1.o/Scrt1.o/crtbegin.o; our crt0.o lacks it). A unique hidden
 * address is all a static binary needs.
 *
 * (History: this file also carried an sbrk(0) shim from when mlibc/rbos
 * had no sbrk. mlibc now provides sbrk via the Brk sysdep served by the
 * kernel sys_brk(12), so the shim was dropped — keeping it gives
 * `multiple definition of sbrk` at link.) */
void *__attribute__((visibility("hidden"))) __dso_handle = 0;
