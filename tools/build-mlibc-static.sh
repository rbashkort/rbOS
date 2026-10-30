#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MLIBC_DIR="$ROOT_DIR/mlibc"
SYSDEPS_DIR="$ROOT_DIR/mlibc-sysdeps"
BUILD_DIR="${MLIBC_DIR}/build-rbos-static"
OUTPUT_DIR="$ROOT_DIR/mlibc-sysdeps/sysdeps/rbos/lib"

echo "=== Building STATIC mlibc for rbOS ==="

if [ ! -d "$MLIBC_DIR" ]; then
    echo "ERROR: mlibc not found at $MLIBC_DIR"
    exit 1
fi

echo "[1/5] Copying rbOS sysdeps..."
rm -rf "${MLIBC_DIR}/sysdeps/rbos"
cp -r "${SYSDEPS_DIR}/sysdeps/rbos" "${MLIBC_DIR}/sysdeps/"

echo "[1.5/5] Patching mlibc public headers..."
"$ROOT_DIR/tools/patch-mlibc-headers.sh"

echo "[1.6/5] Patching mlibc/meson.build to support rbos..."
cd "$MLIBC_DIR"

# Prevent duplicate patching if script is run multiple times
if ! grep -q "host_machine.system() == 'rbos'" meson.build; then
    # Insert rbos handling right before the 'else' error block (no colon in meson syntax)
    sed -i "/^else$/i elif host_machine.system() == 'rbos'\\
\\trtld_include_dirs += include_directories('sysdeps/rbos/include')\\
\\tlibc_include_dirs += include_directories('sysdeps/rbos/include')\\
\\tsubdir('sysdeps/rbos')" meson.build
    echo "    ✓ Patched meson.build to recognize rbos"
else
    echo "    ✓ meson.build already patched for rbos"
fi

echo "[2/5] Creating static cross-compilation configuration..."
cat > "${MLIBC_DIR}/rbos-static-cross.txt" << 'EOF'
[binaries]
c = 'gcc'
cpp = 'g++'
ar = 'ar'
strip = 'strip'

[host_machine]
system = 'rbos'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[built-in options]
c_args = ['-mcmodel=small', '-fno-pic', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-ffreestanding', '-D__rbos__=1', '-D__RBOS_OPENBSD_COMPAT__=1']
cpp_args = ['-mcmodel=small', '-fno-pic', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-D__rbos__=1', '-D__RBOS_OPENBSD_COMPAT__=1']
c_link_args = ['-static', '-nostdlib']
cpp_link_args = ['-static', '-nostdlib']
default_library = 'static'
EOF

echo "[3/6] Staging Linux UAPI headers (mlibc linux option)..."
"$ROOT_DIR/tools/stage-linux-headers.sh"
LINUX_KERNEL_HEADERS="$(realpath "$ROOT_DIR/build/linux-headers")"

echo "[4/6] Cleaning and setting up build directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[5/6] Configuring mlibc (STATIC, NO rtld)..."
meson setup \
    --cross-file ../rbos-static-cross.txt \
    --prefix=/usr \
    --libdir=lib \
    -Ddefault_library=static \
    -Dposix_option=enabled \
    -Dlinux_option=enabled \
    -Dglibc_option=enabled \
    -Dbsd_option=enabled \
    -Dlinux_kernel_headers="$LINUX_KERNEL_HEADERS" \
    -Dheaders_only=false \
    -Dbuild_tests=false \
    ..

echo "[6/6] Building static mlibc..."
ninja

echo ""
echo "[+] Copying artifacts to $OUTPUT_DIR..."
mkdir -p "$OUTPUT_DIR"
find . -name "libc.a" -exec cp {} "$OUTPUT_DIR/" \;
find . -name "libm.a" -exec cp {} "$OUTPUT_DIR/" \;
find . -name "crt*.o" -exec cp {} "$OUTPUT_DIR/" \;
find . -name "rcrt1.o" -exec cp {} "$OUTPUT_DIR/" \;
find . -name "Scrt1.o" -exec cp {} "$OUTPUT_DIR/" \; 2>/dev/null || true

echo ""
echo "=== Static mlibc build complete ==="
echo ""
echo "Artifacts location: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"
echo ""
echo "Compile userspace programs:"
echo "  gcc -mcmodel=small -fno-pic -fno-pie -nostdlib -ffreestanding \\"
echo "      -o program \\"
echo "      ${OUTPUT_DIR}/crt0.o program.c \\"
echo "      -L${OUTPUT_DIR} -lc \\"
echo "      -I${MLIBC_DIR}/sysdeps/rbos/include"
