#ifndef MLIBC_THREAD_ENTRY

#include <mlibc/tcb.hpp>

extern "C" long __mlibc_spawn_thread(unsigned long flags, void *stack, void *pid_out, void *child_tid, void *tls);
extern "C" void __mlibc_enter_thread(void *entry, void *user_arg);

#endif // MLIBC_THREAD_ENTRY
