#!/bin/bash

set -e

# Run from the repository root regardless of the caller's CWD (this script
# moved to tools/, so plain relative paths below would resolve wrongly).
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SYSROOT="sysroot"
MLIBC_DIR="mlibc"
MLIBC_SYSDEPS="mlibc-sysdeps"
MLIBC_BUILD="mlibc/build-rbos-static"

echo "=== Populating sysroot with mlibc ==="

# Create sysroot structure for development
echo "Creating sysroot structure..."
mkdir -p ${SYSROOT}/usr/lib
mkdir -p ${SYSROOT}/usr/include

echo "Installing mlibc libraries..."
# Copy static library and crt objects
cp ${MLIBC_SYSDEPS}/sysdeps/rbos/lib/libc.a ${SYSROOT}/usr/lib/
cp ${MLIBC_SYSDEPS}/sysdeps/rbos/lib/crt0.o ${SYSROOT}/usr/lib/
cp ${MLIBC_SYSDEPS}/sysdeps/rbos/lib/rcrt1.o ${SYSROOT}/usr/lib/ 2>/dev/null || true

echo "Patching mlibc public headers (OpenBSD-compatible declarations)..."
"$ROOT_DIR/tools/patch-mlibc-headers.sh"

echo "Installing mlibc headers..."
cp -r ${MLIBC_DIR}/sysdeps/rbos/include/* ${SYSROOT}/usr/include/
# Every enabled option's public headers. Order matters only for the one
# duplicated name (memory.h): linux first, glibc last, so glibc wins.
for opt in internal ansi posix elf linux linux-epoll linux-eventfd \
           linux-reboot linux-signalfd linux-timerfd linux-wrappers glibc bsd lsb; do
    if [ -d ${MLIBC_DIR}/options/${opt}/include ]; then
        cp -r ${MLIBC_DIR}/options/${opt}/include/* ${SYSROOT}/usr/include/ 2>/dev/null || true
    fi
done

if [ -d ${MLIBC_DIR}/sysdeps/linux/x86_64/arch-include ]; then
    cp -r ${MLIBC_DIR}/sysdeps/linux/x86_64/arch-include/* ${SYSROOT}/usr/include/ 2>/dev/null || true
fi

# Linux UAPI headers (staged by tools/stage-linux-headers.sh). With the linux
# option enabled, installed public headers such as <sys/ioctl.h> and <linux/*>
# pull in <asm/*> at user-compile time, so the kernel headers have to be in the
# sysroot as well.
if [ -d build/linux-headers/linux ]; then
    cp -r build/linux-headers/linux build/linux-headers/asm build/linux-headers/asm-generic ${SYSROOT}/usr/include/
else
    echo "WARNING: build/linux-headers not found (run tools/stage-linux-headers.sh)" >&2
fi

echo "Installing mlibc libraries (static & dynamic)..."
cp ${MLIBC_SYSDEPS}/sysdeps/rbos/lib/libc.a ${SYSROOT}/usr/lib/ 2>/dev/null || true
cp ${MLIBC_SYSDEPS}/sysdeps/rbos/lib/libm.a ${SYSROOT}/usr/lib/ 2>/dev/null || true
cp ${MLIBC_SYSDEPS}/sysdeps/rbos/lib/crt*.o ${SYSROOT}/usr/lib/ 2>/dev/null || true

find mlibc/ -name "libc.so" -exec cp {} ${SYSROOT}/usr/lib/ \; 2>/dev/null || true
find mlibc/ -name "libm.so" -exec cp {} ${SYSROOT}/usr/lib/ \; 2>/dev/null || true
find mlibc/ -name "ld.so" -exec cp {} ${SYSROOT}/usr/lib/ \; 2>/dev/null || true

# Copy generated config headers
mkdir -p ${SYSROOT}/usr/include/mlibc
# Copy generated config headers
mkdir -p ${SYSROOT}/usr/include/mlibc
if [ -f ${MLIBC_BUILD}/mlibc-config.h ]; then
    cp ${MLIBC_BUILD}/mlibc-config.h ${SYSROOT}/usr/include/
    cp ${MLIBC_BUILD}/mlibc-config.h ${SYSROOT}/usr/include/mlibc/
fi
if [ -f ${MLIBC_BUILD}/internal-config.h ]; then
    cp ${MLIBC_BUILD}/internal-config.h ${SYSROOT}/usr/include/
    cp ${MLIBC_BUILD}/internal-config.h ${SYSROOT}/usr/include/mlibc/
fi

# Copy subproject headers (frigg, libsmarter)
if [ -d ${MLIBC_DIR}/subprojects/frigg/include ]; then
    cp -r ${MLIBC_DIR}/subprojects/frigg/include/* ${SYSROOT}/usr/include/ 2>/dev/null || true
fi
if [ -d ${MLIBC_DIR}/subprojects/libsmarter/include ]; then
    cp -r ${MLIBC_DIR}/subprojects/libsmarter/include/* ${SYSROOT}/usr/include/ 2>/dev/null || true
fi

echo ""
echo "=== Sysroot contents ==="
echo "Libraries:"
ls -lh ${SYSROOT}/usr/lib/
echo ""
echo "Total size:"
du -sh ${SYSROOT}/usr/*
echo ""
echo "✅ Sysroot populated successfully"
echo "   Location: ${SYSROOT}/"
echo "   Use 'make filesystem' to create filesystem.img"
