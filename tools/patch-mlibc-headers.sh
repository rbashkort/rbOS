#!/bin/bash
# Apply the tiny rbOS patches to mlibc (no mlibc fork).
#
# mlibc owns <unistd.h> and <stdlib.h>, so the OpenBSD-compatible declarations
# cannot be injected from mlibc-sysdeps without a hook in those headers.  Keep
# the hook itself in mlibc-sysdeps/patches/ and apply it here, idempotently.
#
# Same mechanism carries the two small source fixes found while bringing up
# rbOS (previously edited directly in the vendored tree):
# 0003 strsignal stays quiet on unnamed numbers (oksh probes 0..NSIG at init),
# 0004 rtld derives baseAddress for static-PIE without PT_PHDR.
#
# If mlibc upstream changes the anchor context, patch(1) fails and this script
# aborts the build: a silent loss of the declarations would be worse.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MLIBC_DIR="$ROOT_DIR/mlibc"
PATCH_DIR="$ROOT_DIR/mlibc-sysdeps/patches"

apply_one() {
    local file="$1"
    local patch_file="$2"
    local marker="$3"

    if [ ! -f "$MLIBC_DIR/$file" ]; then
        echo "ERROR: mlibc file not found: $MLIBC_DIR/$file" >&2
        exit 1
    fi
    if grep -qF "$marker" "$MLIBC_DIR/$file"; then
        echo "    ✓ $file already has the rbOS hook"
        return 0
    fi
    if [ ! -f "$patch_file" ]; then
        echo "ERROR: patch file not found: $patch_file" >&2
        exit 1
    fi

    echo "    + applying $(basename "$patch_file")"
    if ! patch -d "$MLIBC_DIR" -p1 --forward --batch --no-backup-if-mismatch \
            < "$patch_file"; then
        echo "ERROR: failed to apply $patch_file" >&2
        echo "       mlibc changed the hook anchor; update the patch in" >&2
        echo "       $PATCH_DIR and rebuild." >&2
        exit 1
    fi
    if ! grep -qF "$marker" "$MLIBC_DIR/$file"; then
        echo "ERROR: patch applied but marker missing in $file" >&2
        exit 1
    fi
}

echo "=== Patching mlibc public headers for OpenBSD-compatible declarations ==="
apply_one \
    "options/posix/include/unistd.h" \
    "$PATCH_DIR/0001-unistd-openbsd-hook.patch" \
    "bits/rbos/openbsd_unistd.h"
apply_one \
    "options/ansi/include/stdlib.h" \
    "$PATCH_DIR/0002-stdlib-openbsd-hook.patch" \
    "bits/rbos/openbsd_stdlib.h"

echo "=== Patching mlibc sources (rbOS fixes) ==="
apply_one \
    "options/posix/generic/posix_string.cpp" \
    "$PATCH_DIR/0003-posix-strsignal-quiet.patch" \
    "Callers are allowed to probe numbers"
apply_one \
    "options/rtld/generic/main.cpp" \
    "$PATCH_DIR/0004-rtld-static-pie-base.patch" \
    "rbOS/static-PIE"

echo "=== mlibc patches applied ==="
