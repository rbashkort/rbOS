// Thread entry trampoline for rbOS sysdeps.
//
// Kept in its own TU: it needs <mlibc/tcb.hpp> (frigg headers), and
// including that from generic.cpp poisons the rest of the file's include
// chain. Mirrors sysdeps/linux/generic/thread.cpp __mlibc_enter_thread.

#include <abi-bits/errno.h>
#include <bits/ensure.h>
#include <frg/mutex.hpp>
#include <mlibc/all-sysdeps.hpp>
#include <mlibc/debug.hpp>
#include <mlibc/thread.hpp>
#include <mlibc/threads.hpp>
#include <mlibc/tcb.hpp>
#include <stdint.h>

extern "C" void __mlibc_enter_thread(void *entry, void *user_arg) {
	auto tcb = mlibc::get_current_tcb();

	// Wait until the parent publishes our TID.
	while(!__atomic_load_n(&tcb->tid, __ATOMIC_RELAXED))
		mlibc::sysdep<FutexWait>(&tcb->tid, 0, nullptr);

	__atomic_fetch_or(&tcb->cancelBits, tcbCancelEnableBit, __ATOMIC_RELAXED);

	tcb->invokeThreadFunc(entry, user_arg);

	mlibc::thread_exit(tcb->returnValue);
}
