# host-tests

Host-built Linux/glibc probes. The point is to test the **kernel** against
real Linux binaries, not against mlibc:

- sources here are compiled with the host `cc`/`c++` and **host glibc**;
- by default they are linked `-static` (rbOS has no glibc `ld.so` yet);
- they are installed into `sysroot/home/hosttests/` and therefore appear in
  `filesystem.img` when you rebuild it;
- installation is explicit and removal is one command.

```sh
make host-tests           # build + install into sysroot/home/hosttests
make test SMP=1           # rebuild the image; probes are inside it
# in rbOS: /home/hosttests/hello, /home/hosttests/pthread, ...
make host-tests-remove    # delete sysroot/home/hosttests again
```

Why opt-in: a static glibc binary is ~0.6–2.5 MiB (debug info stripped), and
four of them add several MiB to the ext2 image. Keep them out of release
images and install only while testing.

## Tests

| binary | what it checks |
|---|---|
| `hello` | startup, `printf`, `malloc`, `getpid`, argv |
| `pthread` | glibc mutex contention, condvar broadcast, `pthread_cond_timedwait` (futex) |
| `tls` | host glibc static TLS: separate `__thread` per thread |
| `cxx` | static libstdc++: iostream, `std::vector`, `std::thread` |

## Notes

- `STATIC=0 make -C host-tests` builds dynamic probes for the future. They
  need the host `ld.so` and glibc shared objects copied into rbOS and are not
  expected to run yet.
- `user/tests/tltest` is the older host-static TLS probe wired into `rbtest`;
  `tls` here is the same idea, kept next to the other host probes.
- Build output stays in `host-tests/build/`; `make -C host-tests clean` removes
  it. `make host-tests-remove` only removes the sysroot copy.
