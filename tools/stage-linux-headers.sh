#!/bin/bash
#
# Stage a Linux UAPI header tree for mlibc's linux option (P3.6).
#
# mlibc requires -Dlinux_kernel_headers=<dir> pointing at a directory that
# holds the kernel UAPI headers (linux/, asm/, asm-generic/) and explicitly
# refuses a system include path (meson.build rejects /usr/include). On a
# normal Linux host those headers live in /usr/include, so we copy them into
# build/linux-headers. For a reproducible build from kernel sources, point
# LINUX_HEADERS_SRC at the output of `make headers_install`
# (usr/include in the kernel tree).
#
# The staged tree is also installed into the sysroot by populate-sysroot.sh,
# because the linux option's public headers (<sys/ioctl.h>, <linux/...>) pull
# in <asm/...> at user-compile time.
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${LINUX_HEADERS_SRC:-/usr/include}"
DST="${LINUX_HEADERS_DST:-$ROOT_DIR/build/linux-headers}"

for d in linux asm asm-generic; do
    if [ ! -d "$SRC/$d" ]; then
        echo "ERROR: $SRC/$d not found (set LINUX_HEADERS_SRC to a UAPI tree)" >&2
        exit 1
    fi
done

rm -rf "$DST"
mkdir -p "$DST"
cp -a "$SRC/linux" "$SRC/asm" "$SRC/asm-generic" "$DST/"

echo "Staged Linux UAPI headers: $DST (from $SRC)"
