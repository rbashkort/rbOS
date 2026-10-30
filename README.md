# rbOS

rbOS is an operating system and the reference distribution around the
**rbKernel** kernel: a userspace (shell, core utilities, ports, tests) that
shows what the kernel can do. If you want the kernel alone, take
[rbKernel](https://github.com/rbashkort/rbkernel) instead — rbOS just demonstrates it.

Status: boots in QEMU (BIOS + UEFI via Limine), runs a shell with job
control, ~150 userspace binaries, and its own test suite green on SMP=1/2/4.

## Requirements

- `qemu-system-x86_64`, `xorriso`, `mke2fs` (e2fsprogs), `mtools` (for `make disk`)
- `gcc`, `meson`, `ninja`, `patch`, `python3 (for meson)`
- Rust **nightly** (`rustup toolchain install nightly`, the kernel needs
  `#![feature(abi_x86_interrupt)]`) + target `x86_64-unknown-none`
- Host `gcc -static` for the optional Linux-compat probes (`host-tests`)

```sh
# Void Linux
sudo xbps-install -S qemu xorriso e2fsprogs mtools gcc meson ninja patch python3

# Ubuntu / Debian
sudo apt update && sudo apt install -y qemu-system-x86 xorriso e2fsprogs mtools build-essential libc6-dev meson ninja-build patch python3

# Arch Linux
sudo pacman -Syu --needed qemu-system-x86 xorriso e2fsprogs mtools base-devel meson ninja python

# Fedora
sudo dnf install -y qemu-system-x86-core xorriso e2fsprogs mtools gcc glibc-static meson ninja-build patch python3

# openSUSE
sudo zypper install -y qemu-x86 xorriso e2fsprogs mtools gcc glibc-devel-static meson ninja patch python3

# Alpine
sudo apk add --no-cache qemu-system-x86_64 xorriso e2fsprogs mtools build-base meson ninja python3
```


### Rustup

``` sh
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"

# Rust toolchain setup
rustup toolchain install nightly
rustup target add x86_64-unknown-none --toolchain nightly
rustup component add rust-src --toolchain nightly
```


## Quick start

```sh
make world        # everything: mlibc + ports + userspace + image + ISO
make run          # boot in QEMU (add SMP=4 for 4 CPUs)
make run SMP=4
make test         # headless test suite, 1 CPU by default
make test SMP=4
make distclean    # wipe every generated artifact
make uninstall    # unstage userspace + ports from sysroot (keeps build trees)
```

`make test` is green when the suite reports `ALL GREEN` on SMP=1/2/4 with
`LINUX-FAIL=0`. The canonical check is the serial log, not the PASS counter.

## Layout

- `kernel/` — the rbKernel kernel (submodule)
- `user/` — native userspace (init, shell setup, tools, editors)
- `user/tests/` — guest test suite (runs under `rbtest`)
- `ports/` — third-party software (oksh, sbase, pixman, tcc, binutils, e2fsck)
- `mlibc/` — C library (submodule, pinned to managarm/mlibc `v7.0.0`);
  rbOS-specific bits live in `mlibc-sysdeps/` and are applied as patches
- `limine/` — bootloader binaries (byte-identical to the upstream `v12.4.1`
  release, see `limine/README.md`)
- `tools/` — build and test scripts
- `sysroot/` — generated staging area (never committed), packed into
  `filesystem.img` and booted as `rbos.iso`

## License

GPLv2 — see `LICENSE`.
