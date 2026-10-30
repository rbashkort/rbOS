// rtld-only stub: ld.so links with --no-undefined and pulls thread_entry.S
// (for __mlibc_spawn_thread), but never creates threads itself. Provide a
// definition of the trampoline target so the link succeeds.

extern "C" __attribute__((visibility("hidden")))
[[noreturn]] void __mlibc_enter_thread(void *, void *) {
	__builtin_trap();
}
