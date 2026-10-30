.section .text

# long __mlibc_spawn_thread(unsigned long flags /*rdi*/,
#                           void *stack         /*rsi*/,
#                           pid_t *parent_tid   /*rdx*/,
#                           void *tcb           /*rcx*/);
#
# Performs Linux-style clone(2) on rbOS (nr 56):
#   clone(flags, newsp, parent_tid, child_tid=NULL, tls=tcb)
# rcx must be moved to r10 (syscall clobbers rcx with return address).
# In the child (rax == 0) the stack top holds {entry, user_arg} pushed by
# PrepareStack; pop them and enter the thread trampoline.
.globl __mlibc_spawn_thread
.type __mlibc_spawn_thread, @function
.hidden __mlibc_spawn_thread
__mlibc_spawn_thread:
	xor %eax, %eax
	mov %rcx, %r10
	mov $56, %eax		/* RB_SYS_CLONE */
	syscall
	test %eax, %eax
	jnz 1f
	xor %ebp, %ebp
	pop %rdi
	pop %rsi
	call __mlibc_enter_thread
	hlt
1:
	ret
.size __mlibc_spawn_thread, . - __mlibc_spawn_thread

.globl __mlibc_enter_thread
.hidden __mlibc_enter_thread
