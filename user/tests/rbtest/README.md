# rbtest — non-interactive test runner for rbOS

Runs every userspace `*test` binary **without a terminal** (no stdin, no
`^C`) and produces a report. Two halves:

- **guest** — `/usr/bin/rbtest` (sources in `user/tests/rbtest`): forks each test,
  captures stdout/stderr through a pipe, kills hangs with a per-test
  watchdog (`SIGKILL` after N seconds), prints `>>> RBTEST ... <<<`
  markers to stdout (mirrored to serial, so the host sees them even when
  the framebuffer is dead) and writes `/tmp/rbtest-report.txt` (+ copy to
  `/home/rbtest-report.txt` on ext2).
- **host** — `tools/rbtest-host.sh`: boots `rbos-test.iso` headless
  (`-display none`, serial to `/tmp/rbtest/rbtest-serial.log`), waits for
  `>>> RBTEST DONE`, kills QEMU on kernel crashes / stalls, prints the
  summary. Exit 0 = all green, 1 = anything failed / crashed / hung.
  A test with `RUN` but no `RESULT` (machine died mid-test) is counted
  as FAIL explicitly, so a kernel crash can never hide behind "0 FAIL".

## Quick start

```sh
make test          # suite, 1 CPU  (same as ./tools/rbtest-host.sh)
make test SMP=4    # suite on 4 CPUs (SMP race watchdog)
make test-iso      # only build rbos-test.iso
make host-tests    # optional: build+install host Linux/glibc probes
```

Host-built Linux/glibc probes (`host-tests/`) are opt-in: `make host-tests`
installs them under `/home/hosttests`, and `make host-tests-remove` deletes
them again. Without them the four `host-*` entries verdict as SKIP.

Manual runs inside the guest (needs a shell):

```sh
rbtest                 # whole suite except interactive tests
rbtest --list          # list tests
rbtest jobtest-tstp    # one test by name
RBTEST_ONLY=fstest rbtest
rbtest --interactive   # also run vttest (needs a human on the VT)
```

Autotest at boot: kernel cmdline `rbtest` makes PID 1 = `/usr/bin/rbtest`
instead of `/sbin/init` (`init=/path` overrides explicitly, Linux-style).
`rbos-test.iso` is built with `cmdline: console=ttyS0 rbtest`.

## How a verdict is decided

Most tests print `PASS:`/`FAIL:` lines and exit non-zero on failure, but
the formats differ (`[ok]`/`[FAIL]`, `ok`/`FAIL`, bare exit codes…),
so each suite entry carries its own rule:

| test | rule |
|---|---|
| abitest, fstest, pthreadtest, setuidtest | `ALL PASS` in output, exit 0 |
| tltest (host-static libc) | `TLS-OK` in output, no `TLS-FAIL` |
| host-hello / host-pthread / host-tls / host-cxx | host-built Linux/glibc probes from `host-tests/`; installed with `make host-tests`, otherwise SKIP (missing binary) |
| stacktest (P2 guard/ASLR) | `STACK-ALL-PASS` in output, no `STACK-FAIL` |
| prctltest (P3.1 prctl) | `PRCTL-OK` in output, no `PRCTL-FAIL` |
| statxtest (P3.2 statx) | `STATX-OK` in output, no `STATX-FAIL` |
| proctest (P3.7 procfs) | `PROC-OK` in output, no `PROC-FAIL` |
| openat2test (P3.3 openat2) | `OPENAT2-OK` in output, no `OPENAT2-FAIL` |
| clone3test (P3.5 clone3) | `CLONE3-OK` in output, no `CLONE3-FAIL` |
| rseqtest (P3.4 rseq) | `RSEQ-OK` in output, no `RSEQ-FAIL` |
| futextest (P4.1 futex ABI) | `FUTEX-OK` in output, no `FUTEX-FAIL` |
| robusttest (P4.2 robust futex) | `ROBUST-OK` in output, no `ROBUST-FAIL` |
| linuxtest (P3.6 mlibc linux option) | `LINUX-OK` in output, no `LINUX-FAIL` |
| syscalltest, devtest | no `[FAIL]`/`FAIL` in output |
| memtest | `PASS`, exit 0 (exit 2 = leak suspected) |
| jobtest-* | exit 0 and no `FAIL` (split per `JOBTEST_ONLY` case, see below) |
| crashtest-{0,1,2} | **death by SIGSEGV/SIGILL/SIGBUS = PASS** (kernel must kill only that process; survival of the runner proves it) |
| true/fetch-smoke | exit 0 |
| fbtest | `successfully` in output |
| vttest | skipped unless `--interactive` (reads Enter from the VT) |

Machine markers (for scripts):

```
>>> RBTEST BEGIN suite=39 tty=1 <<<
>>> RBTEST RUN abitest <<<
--- output abitest (live) ---
...test output streamed live...
--- end output abitest (142 bytes) ---
>>> RBTEST RESULT abitest PASS exit=0 time=0s note=exit 0 <<<
>>> RBTEST SUMMARY passed=21 failed=0 skipped=1 timeout=0 <<<
>>> RBTEST DONE rc=0 <<<
```

## Crashes and hangs: what to expect

- **Userspace crash** (crashtest): kernel prints `[pid N] terminated by
  signal 11`, the runner survives and reports PASS. That is the designed
  behavior, not a failure.
- **Kernel crash** (historical fbtest case: ring0 page fault at
  `RIP FFFFFFFF800018C2`, `CR2 FFFF8000_0000001x` — a `pd_index`/`pdpt_index`
  typo in `unmap_no_shootdown`, fixed; plus a stack-smashing fbtest binary
  with an undersized ioctl buffer, replaced by `user/fbtest/`): the whole
  machine halts, no `DONE` marker. The host script reports
  `KERNEL CRASH (likely in the last RUN test above)`. fbtest runs **last**
  so everything else has reported already.
- **Scheduler wedge** (SMP race): serial goes silent
  with no `EXCEPTION`. The host reports `HANG` with the last started vs.
  last finished test, and counts the stranded test as FAIL. The in-guest
  watchdog (`SIGKILL` + bounded reap — never a blocking `waitpid`, which
  would park the suite forever) covers ordinary userspace hangs; a silent
  kernel needs the host timeout.
- Stall detection and the displayed serial tail ignore the `[idle dbg]`
  heartbeat (gated at `LVL_DBG` in `sched/mod.rs`, i.e. off at the default
  `LVL_EXT` in `log.rs`): surviving CPUs keep printing it after a crash,
  and raw byte counting would never notice the stall.
- Bisecting a hang: run the suspect alone in a throwaway VM, e.g.
  `rbtest jobtest-bgread` from a shell, or boot the test ISO and watch
  where the live output stops.

## Known issues (found by this harness)

- `fbtest` → FIXED (two bugs: kernel ring0 fault in `unmap_no_shootdown`,
  and a stack-smashing test binary). Kept last in the suite as insurance;
  a `RUN` with no `RESULT` still attributes any future regression.
- `jobtest-bgread` (background `read(0)` → `SIGTTIN`) → FIXED and runs in
  the suite now. Two bugs: PID1's ctty was hardcoded to VT tty1 while stdio
  sat on the serial console (so the bg check never stopped, the reader
  parked forever), and `kill(pid>0, SIGKILL)` acted on the whole process
  group — the watchdog killed its own group and the suite died silently.
- Fixed while building this: `abitest` needed `fflush(stdout)` before
  its `dup2`redirection check (lost under piped stdout), and `fstest`
  expected `rbOS` in `/proc/version` which actually says `rbKernel`.
