# e2fsck port for rbOS

Static `e2fsck` (e2fsprogs 1.47.2) built against the rbOS sysroot with
bundled libuuid/libblkid. Installed to `/usr/bin/e2fsck` (strip it on
install to save ~19MB of debug info).

Usage in-guest (no raw disk device exists, so check images/files):

    e2fsck -n -f /path/to/image   # read-only check

## rbOS patches (`patches/`)

All guarded by `-D__rbos__=1` (added by `rbos-cc-wrap.sh`), cosmetic-only:

- `lib_{et,blkid,ext2fs}_*.patch`: skip the `sys/syscall.h` +
  raw-`syscall(SYS_prctl)` dumpable check — mlibc/rbos has neither the
  header nor `syscall()`. Only gates whether error text prints.
- `lib_ext2fs_llseek.c.patch`: skip `<linux/unistd.h>` — 64-bit `off_t`
  takes the plain `lseek` branch, `__NR__llseek` is compiled out.

## Port shims (`sbrk_shim.c`, linked into the binary)

- `sbrk(0)`: resource tracking only; backed by kernel `sys_sbrk(12)`.
  Growing via sbrk returns ENOMEM (malloc uses mmap paths).
- `__dso_handle`: hidden unique address for libgcc/libc destructors
  (glibc puts it in rcrt1.o/crtbegin.o; our crt0.o lacks it).

## Build notes (`build`)

- Configure runs cross (`--host=x86_64-unknown-linux-gnu`, behavior comes
  from the CC wrapper) with `ac_cv_lib_uuid_uuid_generate=no` /
  `ac_cv_lib_blkid_blkid_get_cache=no` so bundled libs are used instead
  of the host ones.
- Final link is manual: the stock `e2fsck.static` target pulls host
  `-ldl -lpthread`. Only `lib/{uuid,blkid,et,e2p,support,ext2fs}` are
  needed (no libss).
- mlibc noise (`__ensure ... Pread/Fsync ...`, sysconf fallbacks) is
  cosmetic: unix_io falls back to lseek+read, results are correct.

## Known limitations (v0.1)

- No `/dev/ata0`: the live rootfs cannot be checked from inside the
  guest; check images/files, or the host copy of `filesystem.img`.
- `e2fsck -y` (repair) on a mounted filesystem is unsafe — same rule as
  Linux; always check copies or offline images.
- Upstreamable kernel/libc work (not done): `pread64`/`pwrite64`
  syscalls + mlibc Pread/Pwrite sysdeps (silences fallback noise),
  `prctl` stub, `__dso_handle` in crt0.o.
