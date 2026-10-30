# rbOS sysdeps meson.build

# Declare supported options
sysdep_supported_options = {
	'posix': true,
	'linux': true,
	'glibc': true,
	'bsd': true,
}

# Add include directories for abi-bits headers
rbos_include_dirs = [
	include_directories('include'),
]

libc_include_dirs += rbos_include_dirs
rtld_include_dirs += rbos_include_dirs

libc_sources += files(
	'generic.cpp',
	'entry.cpp',
	'libc_extras.c',
	'rbos_ext.c',
	'enter_thread.cpp',
	'thread_entry.S'
)

rtld_sources += files(
    'generic.cpp',
    'thread_entry.S',
    'rtld_stubs.cpp'
)

# Install ABI headers
if not no_headers
	install_headers(
		'include/abi-bits/abi.h',
		'include/abi-bits/access.h',
		'include/abi-bits/aio.h',
		'include/abi-bits/auxv.h',
		'include/abi-bits/blkcnt_t.h',
		'include/abi-bits/blksize_t.h',
		'include/abi-bits/clockid_t.h',
		'include/abi-bits/dev_t.h',
		'include/abi-bits/errno.h',
		'include/abi-bits/fcntl.h',
		'include/abi-bits/fd_set.h',
		'include/abi-bits/fsblkcnt_t.h',
		'include/abi-bits/fsfilcnt_t.h',
		'include/abi-bits/gid_t.h',
		'include/abi-bits/in.h',
		'include/abi-bits/ino_t.h',
		'include/abi-bits/ioctls.h',
		'include/abi-bits/ipc.h',
		'include/abi-bits/limits.h',
		'include/abi-bits/mode_t.h',
		'include/abi-bits/mqueue.h',
		'include/abi-bits/msg.h',
		'include/abi-bits/nlink_t.h',
		'include/abi-bits/pid_t.h',
		'include/abi-bits/poll.h',
		'include/abi-bits/resource.h',
		'include/abi-bits/riscv-hwprobe.h',
		'include/abi-bits/rlim_t.h',
		'include/abi-bits/sa_family_t.h',
		'include/abi-bits/seek-whence.h',
		'include/abi-bits/sem.h',
		'include/abi-bits/shm.h',
		'include/abi-bits/sigevent.h',
		'include/abi-bits/sig-limits.h',
		'include/abi-bits/signal.h',
		'include/abi-bits/sigset_t.h',
		'include/abi-bits/sigval.h',
		'include/abi-bits/sockaddr_storage.h',
		'include/abi-bits/socket.h',
		'include/abi-bits/socklen_t.h',
		'include/abi-bits/stat.h',
		'include/abi-bits/statvfs.h',
		'include/abi-bits/suseconds_t.h',
		'include/abi-bits/termios.h',
		'include/abi-bits/time.h',
		'include/abi-bits/uid_t.h',
		'include/abi-bits/utmp-defines.h',
		'include/abi-bits/utmpx.h',
		'include/abi-bits/utsname.h',
		'include/abi-bits/vm-flags.h',
		'include/abi-bits/wait.h',
		subdir: 'abi-bits',
		follow_symlinks: true
	)
	# P7: rbOS-specific public extensions (experimental APIs only).
	install_headers('include/rbos.h')
	# P7.0b: OpenBSD standard-header declarations, injected by the tiny
	# mlibc header hooks from mlibc-sysdeps/patches/.
	install_headers(
		'include/bits/rbos/openbsd_unistd.h',
		'include/bits/rbos/openbsd_stdlib.h',
		subdir: 'bits/rbos'
	)
endif

# Build and install crt0
if not headers_only
	crt = custom_target('crt0',
		build_by_default: true,
		command: c_compiler.cmd_array() + ['-c', '-o', '@OUTPUT@', '@INPUT@'],
		input: 'crt-x86_64/crt0.S',
		output: 'crt0.o',
		install: true,
		install_dir: get_option('libdir')
	)
	# rcrt1: startup for static PIE (self-relocation + __dso_handle).
	# Used instead of crt0.o when linking with -static-pie.
	rcrt1 = custom_target('rcrt1',
		build_by_default: true,
		command: c_compiler.cmd_array() + ['-c', '-o', '@OUTPUT@', '@INPUT@'],
		input: 'crt-x86_64/rcrt1.S',
		output: 'rcrt1.o',
		install: true,
		install_dir: get_option('libdir')
	)
endif

