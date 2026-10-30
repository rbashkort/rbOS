#!/bin/bash
# Strip debug information from the deployment copy of the sysroot.
#
# Build artifacts (mlibc/build-*, ports/gnu/build, user/*/build) keep their
# debug info; only what lands in sysroot/ (and therefore in filesystem.img) is
# stripped. Static archives keep their symbols/relocations (needed for linking)
# and only lose .debug_*; executables and shared objects are fully stripped.
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SYSROOT="${SYSROOT:-sysroot}"

if [ ! -d "$SYSROOT" ]; then
    echo "strip-sysroot: $SYSROOT does not exist, nothing to do"
    exit 0
fi

is_elf() {
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    [ "$(head -c 4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]
}

before=$(du -sb "$SYSROOT" 2>/dev/null | cut -f1)

# Static archives: drop debug sections only; keep .symtab and relocations so
# `rbos-cc -static` can still link against libc.a/libtcc1.a/etc.
while IFS= read -r -d '' f; do
    is_elf "$f" || continue
    strip --strip-debug "$f" 2>/dev/null || true
done < <(find "$SYSROOT" -type f -name '*.a' -print0)

# Executables and shared objects: full strip. `.dynsym`/.dynamic survive, so
# the dynamic loader and `-dynamic` binaries remain functional.
while IFS= read -r -d '' f; do
    is_elf "$f" || continue
    case "$f" in
        # Reference host static binary kept byte-identical on purpose.
        "$SYSROOT/home/test") continue ;;
    esac
    strip -s "$f" 2>/dev/null || strip --strip-debug "$f" 2>/dev/null || true
done < <(find "$SYSROOT" -type f \( -perm -u+x -o -name '*.so' -o -name '*.so.*' \) -print0)

after=$(du -sb "$SYSROOT" 2>/dev/null | cut -f1)
printf 'strip-sysroot: %d MiB -> %d MiB\n' \
    "$((before / 1024 / 1024))" "$((after / 1024 / 1024))"
