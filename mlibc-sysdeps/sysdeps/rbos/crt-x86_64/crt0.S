.section .text
.global _start

_start:
    xor %ebp, %ebp

    mov %rsp, %rdi

    lea main(%rip), %rsi

    call __mlibc_entry

    mov %rax, %rdi
    mov $2, %rax
    syscall
    hlt
