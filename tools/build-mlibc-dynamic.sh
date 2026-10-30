#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MLIBC_DIR="$ROOT_DIR/mlibc"
SYSDEPS_DIR="$ROOT_DIR/mlibc-sysdeps"
BUILD_DIR="${MLIBC_DIR}/build-rbos-dynamic"
OUTPUT_DIR="$ROOT_DIR/mlibc-sysdeps/sysdeps/rbos/lib-dynamic"

echo "=== Building DYNAMIC mlibc for rbOS (with rtld) ==="

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

echo "[2/5] Creating dynamic cross-compilation configuration..."
cat > "${MLIBC_DIR}/rbos-dynamic-cross.txt" << 'EOF'
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
c_args = ['-mcmodel=small', '-fPIC', '-fno-stack-protector', '-nostdlib', '-ffreestanding', '-D__rbos__=1', '-D__RBOS_OPENBSD_COMPAT__=1']
cpp_args = ['-mcmodel=small', '-fPIC', '-fno-stack-protector', '-nostdlib', '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-D__rbos__=1', '-D__RBOS_OPENBSD_COMPAT__=1']
c_link_args = ['-nostdlib']
cpp_link_args = ['-nostdlib']
default_library = 'shared'
EOF

echo "[3/6] Staging Linux UAPI headers (mlibc linux option)..."
"$ROOT_DIR/tools/stage-linux-headers.sh"
LINUX_KERNEL_HEADERS="$(realpath "$ROOT_DIR/build/linux-headers")"

echo "[4/6] Cleaning and setting up build directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[5/6] Configuring mlibc (DYNAMIC, with rtld)..."
meson setup \
    --cross-file ../rbos-dynamic-cross.txt \
    --prefix=/usr \
    --libdir=lib \
    -Ddefault_library=shared \
    -Dposix_option=enabled \
    -Dlinux_option=enabled \
    -Dglibc_option=enabled \
    -Dbsd_option=enabled \
    -Dlinux_kernel_headers="$LINUX_KERNEL_HEADERS" \
    -Dheaders_only=false \
    -Dbuild_tests=false \
    ..

echo "[6/6] Building dynamic mlibc with rtld..."
ninja

echo ""
echo "[+] Copying artifacts to $OUTPUT_DIR..."
mkdir -p "$OUTPUT_DIR"
find . -name "libc.so*" -exec cp -a {} "$OUTPUT_DIR/" \;
find . -name "libm.so*" -exec cp -a {} "$OUTPUT_DIR/" \;
find . -name "ld.so*" -exec cp -a {} "$OUTPUT_DIR/" \;
find . -name "ld-rbos.so*" -exec cp -a {} "$OUTPUT_DIR/" \;
find . -name "crt*.o" -exec cp {} "$OUTPUT_DIR/" \;
find . -name "Scrt1.o" -exec cp {} "$OUTPUT_DIR/" \; 2>/dev/null || true

echo ""
echo "=== Dynamic mlibc build complete ==="
echo ""
echo "Artifacts location: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"
echo ""
echo "Install to sysroot:"
echo "  mkdir -p sysroot/usr/lib"
echo "  cp ${OUTPUT_DIR}/ld.so* sysroot/usr/lib/"
echo "  cp ${OUTPUT_DIR}/libc.so* sysroot/usr/lib/"
echo ""
echo "Compile dynamic programs:"
echo "  gcc -mcmodel=small -fPIC -nostdlib -ffreestanding \\"
echo "      -o program \\"
echo "      ${OUTPUT_DIR}/crt1.o program.c \\"
echo "      -L${OUTPUT_DIR} -lc \\"
echo "      -dynamic-linker /usr/lib/ld.so \\"
echo "      -I${MLIBC_DIR}/sysdeps/rbos/include"
