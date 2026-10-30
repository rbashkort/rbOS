#!/usr/bin/env bash
# rbtest-host.sh: run the rbOS autotester headlessly and report.
#
# Boots rbos-test.iso (built by `make test-iso`, cmdline `console=ttyS0 rbtest`
# so PID1 is /usr/bin/rbtest, no terminal input needed), captures the serial
# log, waits for the `>>> RBTEST DONE` marker and prints a summary.
#
# Kernel faults (fbtest ring0 page fault) and SMP deadlocks never produce
# DONE: the guest sits in `cli;hlt` or spins silently. Both are detected
# here — EXCEPTION/PANIC lines attribute a crash, a stalled log attributes
# a hang — and QEMU is killed so CI never blocks forever.
#
# Usage: tools/rbtest-host.sh [--iso rbos-test.iso] [--img filesystem.img]
#        [--disk disk.img] [--smp N] [--timeout SEC] [--stall SEC]
#        [--log FILE] [--mem MB]
#
# Default mode boots rbos-test.iso as cdrom plus filesystem.img as IDE disk.
# --disk mode boots a self-contained Limine-BIOS disk image (`make disk`)
# as the IDE disk instead (no cdrom): same watchdog, same DONE detection.
set -u

ISO="rbos-test.iso"
IMG="filesystem.img"
DISK=""
SMP="1"
TIMEOUT="240"
STALL="45"
MEM="256M"
LOG="/tmp/rbtest/rbtest-serial.log"
CPU="qemu64,+smep,+smap"

while [ $# -gt 0 ]; do
    case "$1" in
        --iso) ISO="$2"; shift 2;;
        --img) IMG="$2"; shift 2;;
        --disk) DISK="$2"; shift 2;;
        --smp) SMP="$2"; shift 2;;
        --timeout) TIMEOUT="$2"; shift 2;;
        --stall) STALL="$2"; shift 2;;
        --log) LOG="$2"; shift 2;;
        --mem) MEM="$2"; shift 2;;
        --cpu) CPU="$2"; shift 2;;
        -h|--help) sed -n '2,20p' "$0"; exit 0;;
        *) echo "unknown arg: $1" >&2; exit 2;;
    esac
done

if [ -n "$DISK" ]; then
    if [ ! -f "$DISK" ]; then
        echo "error: disk image $DISK not found. Run 'make disk' first." >&2
        exit 2
    fi
    BOOT_DRIVES=(-drive "file=$DISK,format=raw,if=ide,cache=writethrough")
    BOOT_ORDER=c
else
    if [ ! -f "$ISO" ]; then
        echo "error: ISO $ISO not found. Run 'make test-iso' first." >&2
        exit 2
    fi
    if [ ! -f "$IMG" ]; then
        echo "error: disk image $IMG not found. Run 'make filesystem' first." >&2
        exit 2
    fi
    BOOT_DRIVES=(-cdrom "$ISO" -drive "file=$IMG,format=raw,if=ide,cache=writethrough")
    BOOT_ORDER=d
fi
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "error: qemu-system-x86_64 not installed" >&2
    exit 2
fi

mkdir -p "$(dirname "$LOG")"
rm -f "$LOG"
touch "$LOG"

if [ -n "$DISK" ]; then
    echo "=== rbOS autotester (disk: $DISK) ==="
    echo "disk: $DISK  smp: $SMP  mem: $MEM"
else
    echo "=== rbOS autotester ==="
    echo "iso: $ISO  img: $IMG  smp: $SMP  mem: $MEM"
fi
echo "log: $LOG  timeout: ${TIMEOUT}s  stall: ${STALL}s"

# -display none keeps the VGA/framebuffer device (fbtest needs /dev/fb0)
# while showing no window; serial goes to a file for parsing.
# -no-reboot: a triple fault exits QEMU instead of reboot-looping.
qemu-system-x86_64 \
    -cpu "$CPU" \
    "${BOOT_DRIVES[@]}" \
    -display none \
    -serial file:"$LOG" \
    -m "$MEM" \
    -boot "$BOOT_ORDER" \
    -smp "$SMP" \
    -no-reboot \
    -monitor none \
    >/dev/null 2>&1 &
QEMU_PID=$!

cleanup() {
    if kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null
        sleep 1
        if kill -0 "$QEMU_PID" 2>/dev/null; then
            kill -9 "$QEMU_PID" 2>/dev/null
        fi
    fi
    wait "$QEMU_PID" 2>/dev/null
}
trap cleanup EXIT INT TERM

elapsed=0
last_size=0
stalled=0
result="HANG"
while [ "$elapsed" -lt "$TIMEOUT" ]; do
    sleep 1
    elapsed=$((elapsed + 1))
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        # QEMU exited on its own (triple fault + -no-reboot, or user poweroff).
        if grep -q ">>> RBTEST DONE" "$LOG" 2>/dev/null; then
            result="DONE"
        else
            result="QEMU-EXIT"
        fi
        break
    fi
    if grep -q ">>> RBTEST DONE" "$LOG" 2>/dev/null; then
        result="DONE"
        # Let the guest settle (rbtest parks as PID1), then stop QEMU.
        sleep 2
        break
    fi
    # Progress ignores the `[idle dbg]` heartbeat: at LVL_DBG surviving
    # CPUs keep printing it after a crash/wedge, which would otherwise
    # reset the stall counter forever and push every hang to --timeout.
    size=$(grep -v "\[idle dbg\]" "$LOG" 2>/dev/null | wc -c)
    if [ "$size" != "$last_size" ]; then
        last_size="$size"
        stalled=0
    else
        stalled=$((stalled + 1))
        if [ "$stalled" -ge "$STALL" ]; then
            result="STALL"
            break
        fi
    fi
done

# Stop the guest before parsing (cleanup trap also covers signals).
if kill -0 "$QEMU_PID" 2>/dev/null; then
    kill "$QEMU_PID" 2>/dev/null
    sleep 1
    if kill -0 "$QEMU_PID" 2>/dev/null; then
        kill -9 "$QEMU_PID" 2>/dev/null
    fi
fi
wait "$QEMU_PID" 2>/dev/null
trap - EXIT INT TERM

echo ""
echo "=== serial tail (last 30 lines, heartbeat hidden) ==="
grep -v "\[idle dbg\]" "$LOG" 2>/dev/null | tail -n 30 || true
echo "(hidden $(grep -c "\[idle dbg\]" "$LOG" 2>/dev/null || echo 0) [idle dbg] lines; full log: $LOG)"
echo ""

PASS=0; FAIL=0; SKIP=0; TMOUT=0
if grep -q ">>> RBTEST RESULT" "$LOG" 2>/dev/null; then
    echo "=== per-test results ==="
    grep ">>> RBTEST RESULT" "$LOG" | while read -r line; do
        # >>> RBTEST RESULT <name> <verdict> exit=.. time=..s note=.. <<<
        name=$(echo "$line" | awk '{print $4}')
        verdict=$(echo "$line" | awk '{print $5}')
        rest=$(echo "$line" | sed 's/.*exit=/exit=/; s/ <<<//')
        printf "%-14s %-8s %s\n" "$name" "$verdict" "$rest"
    done
    PASS=$(grep -c ">>> RBTEST RESULT .* PASS " "$LOG" || true)
    FAIL=$(grep -c ">>> RBTEST RESULT .* FAIL " "$LOG" || true)
    SKIP=$(grep -c ">>> RBTEST RESULT .* SKIP " "$LOG" || true)
    TMOUT=$(grep -c ">>> RBTEST RESULT .* TIMEOUT " "$LOG" || true)
    # TIMEOUT counts as failure too (rbtest already does that in SUMMARY).
    echo ""
    grep ">>> RBTEST SUMMARY" "$LOG" | tail -n 1 || true
fi

# A test that started (RUN) but never reported (RESULT) died with the
# machine: kernel crash or scheduler wedge. It is a FAIL, not a silent
# omission — otherwise "21 PASS, 0 FAIL" hides the crashed fbtest.
LAST_RUN=$(grep ">>> RBTEST RUN" "$LOG" 2>/dev/null | tail -n 1 | awk '{print $4}' || true)
LAST_RES=$(grep ">>> RBTEST RESULT" "$LOG" 2>/dev/null | tail -n 1 | awk '{print $4}' || true)
STRANDED=""
if [ -n "$LAST_RUN" ] && [ "$LAST_RUN" != "$LAST_RES" ]; then
    STRANDED="$LAST_RUN"
    FAIL=$((FAIL + 1))
fi

echo ""
case "$result" in
    DONE)
        if grep -q ">>> RBTEST DONE rc=0" "$LOG" 2>/dev/null; then
            echo "HOST: suite finished: PASS=$PASS FAIL=$FAIL SKIP=$SKIP TIMEOUT=$TMOUT"
            echo "HOST: ALL GREEN"
            exit 0
        else
            echo "HOST: suite finished with failures: PASS=$PASS FAIL=$FAIL SKIP=$SKIP TIMEOUT=$TMOUT"
            # Surface kernel-side noise that in-guest verdicts may have missed.
            grep -E "EXCEPTION|KERNEL PANIC|terminated by signal" "$LOG" | sort | uniq -c | head -n 20 || true
            exit 1
        fi
        ;;
    STALL|HANG)
        echo "HOST: no DONE marker, log stalled ${stalled}s of ${elapsed}s elapsed."
        last_run=$(grep ">>> RBTEST RUN" "$LOG" 2>/dev/null | tail -n 1 || true)
        last_res=$(grep ">>> RBTEST RESULT" "$LOG" 2>/dev/null | tail -n 1 || true)
        echo "HOST: last started: ${last_run:-<none>}"
        echo "HOST: last finished: ${last_res:-<none>}"
        if [ -n "$STRANDED" ]; then
            printf "%-14s %-8s %s\n" "$STRANDED" "FAIL" "exit=-1 (no RESULT: kernel crash or wedge)"
        fi
        if grep -q "!!! EXCEPTION" "$LOG" 2>/dev/null; then
            echo "HOST: kernel EXCEPTION present (ring0 fault — whole machine is down):"
            grep -n "EXCEPTION" "$LOG" | head -n 10 || true
            echo "HOST: verdict: KERNEL CRASH (likely in the last RUN test above)"
        else
            echo "HOST: no kernel exception — verdict: HANG (SMP deadlock or wedged test;"
            echo "HOST: per-test watchdogs should have killed a userspace hang, so a"
            echo "HOST: stall here usually means the kernel itself stopped scheduling)."
        fi
        echo "HOST: results before stall: PASS=$PASS FAIL=$FAIL SKIP=$SKIP TIMEOUT=$TMOUT"
        exit 1
        ;;
    QEMU-EXIT)
        echo "HOST: QEMU exited early without DONE (triple fault? check log):"
        grep -nE "EXCEPTION|PANIC|Triple|assertion|fatal" "$LOG" | head -n 20 || true
        if [ -n "$STRANDED" ]; then
            printf "%-14s %-8s %s\n" "$STRANDED" "FAIL" "exit=-1 (no RESULT: QEMU exited)"
        fi
        echo "HOST: results before exit: PASS=$PASS FAIL=$FAIL SKIP=$SKIP TIMEOUT=$TMOUT"
        exit 1
        ;;
esac
