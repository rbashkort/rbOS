#!/bin/bash
# Remove the rbOS autotest binaries from the deployed sysroot.
#
# This script is deliberately conservative: it removes an explicit allowlist
# of test executables from sysroot/usr/bin and nothing else. It never expands
# globs, never follows symlinks, and refuses to operate when the sysroot looks
# unexpected. In particular:
#   * /usr/bin/test and /usr/bin/[ are POSIX utilities (hardlinked sbase
#     programs), NOT rbOS tests, so they are never touched;
#   * sysroot/home/test, sysroot/home/test.c and sysroot/home/test.txt are
#     user data and are never touched;
#   * host probes under sysroot/home/hosttests are handled separately by
#     `make host-tests-remove` (which `make tests-remove` calls).
#
# Usage:
#   tools/remove-tests.sh [--dry-run]
#
# Called by `make tests-remove`.

set -euo pipefail

DRY_RUN=0
case "${1:-}" in
    "") ;;
    --dry-run|-n) DRY_RUN=1 ;;
    *)
        echo "usage: $0 [--dry-run]" >&2
        exit 2
        ;;
esac

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYSROOT="$ROOT_DIR/sysroot"
BIN_DIR="$SYSROOT/usr/bin"

# Exact allowlist: every rbOS test executable plus the no-libc stackgeo helper
# used by stacktest. Do not add globs or generated names here.
TEST_BINS=(
    abitest clone3test crashtest devtest fbtest fdtest fstest futextest jobtest
    linuxtest memtest openat2test prctltest proctest pthreadtest rbtest
    robusttest rseqtest setuidtest stacktest stackgeo statxtest
    syscalltest tltest vttest pledgetest pledge-helper randomtest headertest
    unveiltest unveil-helper sysctltest
)

# Paths that must survive this script byte-for-byte (where they exist).
PROTECTED=(
    "$BIN_DIR/test"
    "$BIN_DIR/["
    "$SYSROOT/home/test"
    "$SYSROOT/home/test.c"
    "$SYSROOT/home/test.txt"
)

die() {
    echo "tests-remove: $*" >&2
    exit 1
}

warn() {
    echo "tests-remove: warning: $*" >&2
}

is_elf() {
    [ "$(head -c 4 -- "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]
}

# An absent sysroot is a no-op, not an error (e.g. `make distclean`).
if [ ! -e "$SYSROOT" ] && [ ! -L "$SYSROOT" ]; then
    echo "tests-remove: $SYSROOT does not exist; nothing to remove"
    exit 0
fi

[ ! -L "$SYSROOT" ] || die "refusing to operate on a symlinked sysroot: $SYSROOT"
[ -d "$SYSROOT" ] || die "$SYSROOT is not a directory"

sysroot_real="$(realpath -m -- "$SYSROOT")"
[ "$sysroot_real" = "$ROOT_DIR/sysroot" ] ||
    die "refusing non-standard sysroot: $sysroot_real (expected $ROOT_DIR/sysroot)"

# If the layout is not there yet, there is nothing to delete; still validate
# any existing components so a symlinked usr/bin cannot redirect removals.
if [ ! -e "$BIN_DIR" ] && [ ! -L "$BIN_DIR" ]; then
    echo "tests-remove: $BIN_DIR does not exist; no test binaries to remove"
    exit 0
fi

[ ! -L "$SYSROOT/usr" ] || die "refusing symlinked sysroot/usr: $SYSROOT/usr"
[ ! -L "$BIN_DIR" ] || die "refusing symlinked sysroot/usr/bin: $BIN_DIR"
[ -d "$BIN_DIR" ] || die "$BIN_DIR is not a directory"

bin_real="$(realpath -m -- "$BIN_DIR")"
case "$bin_real" in
    "$sysroot_real"/usr/bin) ;;
    *) die "refusing unexpected binary directory: $bin_real" ;;
esac

# Defensive validation of the allowlist itself.
for name in "${TEST_BINS[@]}"; do
    case "$name" in
        ''|.|..|*/*) die "invalid allowlist entry: $name" ;;
    esac
done

# Snapshot the protected paths so the final check catches accidental changes.
declare -A PROTECTED_STATE=()
for path in "${PROTECTED[@]}"; do
    if [ -e "$path" ] || [ -L "$path" ]; then
        PROTECTED_STATE["$path"]="$(stat -c '%d:%i:%s:%Y' -- "$path")"
        echo "tests-remove: preserving $path"
    fi
done

# Preflight every path first: if anything looks suspicious, remove nothing.
to_remove=()
absent=0
refused=0

for name in "${TEST_BINS[@]}"; do
    path="$BIN_DIR/$name"

    if [ -L "$path" ]; then
        warn "$path is a symlink; refusing to follow or remove it"
        refused=$((refused + 1))
        continue
    fi
    if [ ! -e "$path" ]; then
        absent=$((absent + 1))
        continue
    fi
    if [ ! -f "$path" ]; then
        warn "$path is not a regular file; refusing to remove it"
        refused=$((refused + 1))
        continue
    fi

    links="$(stat -c '%h' -- "$path")"
    if [ "$links" -ne 1 ]; then
        warn "$path has $links hard links; refusing to remove it"
        refused=$((refused + 1))
        continue
    fi
    if ! is_elf "$path"; then
        warn "$path is not an ELF executable; refusing to remove it"
        refused=$((refused + 1))
        continue
    fi

    to_remove+=("$path")
done

if [ "$refused" -ne 0 ]; then
    die "refused to remove $refused suspicious path(s); nothing was deleted"
fi

if [ "${#to_remove[@]}" -gt 0 ]; then
    for path in "${to_remove[@]}"; do
        if [ "$DRY_RUN" -eq 1 ]; then
            echo "tests-remove: would remove $path"
        else
            rm -f -- "$path"
            echo "tests-remove: removed $path"
        fi
    done
fi
removed=${#to_remove[@]}

# Verify that no protected path moved or disappeared.
for path in "${PROTECTED[@]}"; do
    [ -n "${PROTECTED_STATE[$path]+x}" ] || continue
    now="$(stat -c '%d:%i:%s:%Y' -- "$path" 2>/dev/null || echo MISSING)"
    [ "$now" = "${PROTECTED_STATE[$path]}" ] ||
        die "protected path changed unexpectedly: $path"
done

if [ "$DRY_RUN" -eq 1 ]; then
    echo "tests-remove: dry run: would remove $removed file(s), $absent already absent"
else
    echo "tests-remove: removed $removed file(s), $absent already absent"
fi
