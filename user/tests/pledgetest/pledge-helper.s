/* pledge-helper: static, no-libc exec target for pledgetest.
 *
 * It performs exactly one path syscall: open("/home/test.txt", O_RDONLY).
 * Under an inherited "rpath" promise it exits 0; without rpath the kernel
 * kills it with SIGABRT before the syscall returns. exit(2)=60 is handled
 * before the pledge check, so the helper can always terminate.
 */
    .global _start
    .text
_start:
    /* open("/home/test.txt", O_RDONLY, 0) */
    mov     $2, %eax
    lea     .Lpath(%rip), %rdi
    xor     %esi, %esi
    xor     %edx, %edx
    syscall
    test    %rax, %rax
    js      .Lfail

    /* exit(0) */
    mov     $60, %eax
    xor     %edi, %edi
    syscall
    ud2

.Lfail:
    /* open failed but pledge allowed it: exit(1), parent reports failure. */
    mov     $60, %eax
    mov     $1, %edi
    syscall
    ud2

    .section .rodata
.Lpath:
    .asciz "/home/test.txt"

    .section .note.GNU-stack,"",@progbits
