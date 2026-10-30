#!/bin/sh
# CC wrapper for configuring e2fsprogs against the rbOS sysroot.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MLIBC_LIB="$ROOT/sysroot/usr/lib"
COMPAT=""
[ "${RBOS_OPENBSD_COMPAT:-0}" = "1" ] && COMPAT="-D__RBOS_OPENBSD_COMPAT__=1"
BASE="gcc -Wall -O2 -mcmodel=small -fno-pic -fno-pie -fno-stack-protector \
    -mno-red-zone -nostdlib -ffreestanding -nostdinc -D__rbos__=1 $COMPAT \
    -isystem $ROOT/sysroot/usr/include \
    -isystem $ROOT/mlibc/options/posix/include \
    -isystem $ROOT/mlibc/options/ansi/include \
    -isystem $ROOT/mlibc/options/bits/include \
    -isystem $ROOT/mlibc-sysdeps/sysdeps/rbos/include \
    -isystem $(gcc -print-file-name=include)"
case " $* " in
    *" -c "*|*" -E "*|*" -S "*|*" -M"*)
        exec $BASE "$@" ;;
    *)
        # --sysroot hides host libs: an -l missing from the sysroot must
        # fail loudly, not silently resolve to a host .so (cf. libm.so.6).
        # Post-link check: every DT_NEEDED must resolve inside the sysroot,
        # or the guest rtld will panic with "Could not satisfy dependency".
        $BASE -nostdlib -no-pie -L$MLIBC_LIB --sysroot="$ROOT/sysroot" \
            $MLIBC_LIB/crt0.o "$@" -lc || exit 1
        _out="a.out"; _prev=""
        for _a in "$@"; do
            if [ "$_prev" = "-o" ]; then _out="$_a"; break; fi
            case "$_a" in -o?*) _out="${_a#-o}"; break ;; esac
            _prev="$_a"
        done
        for _lib in $(readelf -d "$_out" 2>/dev/null | grep -oP 'Shared library: \[\K[^]]+'); do
            _lib=$(basename "$_lib")
            if [ ! -f "$ROOT/sysroot/lib/$_lib" ] && [ ! -f "$ROOT/sysroot/usr/lib/$_lib" ]; then
                echo "rbos-cc-wrap: error: $_out needs $_lib, NOT in sysroot (host leak?)" >&2
                rm -f "$_out"
                exit 1
            fi
        done ;;
esac
