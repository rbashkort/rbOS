/* stackgeo: P2 gap-geometry probe. Pure _start, no libc at all.
 *
 * At entry the kernel has just set FS to the kernel-placed TLS/TCB below
 * the stack reserve (nothing ran yet to overwrite it — that is the point:
 * any libc startup would mmap its own TLS and hide the geometry). Prints:
 *   GEO-RSP <hex-entry-rsp>
 *   GEO-FS  <hex-fs-base>
 * exit 0. On arch_prctl failure prints GEO-FAIL, exit 1.
 *
 * Syscalls used (Linux x86-64 numbers, same in rbOS): write=1,
 * arch_prctl=158, exit=60. ARCH_GET_FS=0x1003.
 */
    .global _start
    .text
_start:
    mov     %rsp, %r12              /* entry rsp, callee-saved */
    sub     $64, %rsp               /* scratch (top pages are premapped) */
    mov     %rsp, %r13              /* &fs_out */

    /* arch_prctl(ARCH_GET_FS, &fs_out) */
    mov     $158, %eax
    mov     $0x1003, %edi
    mov     %r13, %rsi
    syscall
    test    %rax, %rax
    jnz     .Lfail
    mov     (%r13), %r14            /* fs base, callee-saved */
    test    %r14, %r14
    jz      .Lfail

    /* write(1, "GEO-RSP ", 8) */
    mov     $1, %eax
    mov     $1, %edi
    lea     .Lrsp_tag(%rip), %rsi
    mov     $8, %edx
    syscall

    /* hex(entry rsp) + '\n' */
    mov     %r12, %rdi
    call    print_hex64_nl

    /* write(1, "GEO-FS ", 7) */
    mov     $1, %eax
    mov     $1, %edi
    lea     .Lfs_tag(%rip), %rsi
    mov     $7, %edx
    syscall

    mov     %r14, %rdi
    call    print_hex64_nl

    /* exit(0) */
    mov     $60, %eax
    xor     %edi, %edi
    syscall
    ud2                             /* unreachable */

.Lfail:
    mov     $1, %eax
    mov     $1, %edi
    lea     .Lfail_msg(%rip), %rsi
    mov     $9, %edx
    syscall
    mov     $60, %eax
    mov     $1, %edi
    syscall
    ud2

/* print_hex64_nl: rdi = value. Prints 16 lowercase hex digits + '\n'
 * via write(1). Clobbers rax/rsi/rdx/rcx. r12-r14 preserved. */
print_hex64_nl:
    lea     hexbuf_store(%rip), %rsi
    mov     $60, %ecx               /* shift, 60..0 step 4 */
.Lhex_loop:
    mov     %rdi, %rax
    shr     %cl, %rax
    and     $0xF, %al
    cmp     $10, %al
    jb      .Ldigit
    add     $('a'-10), %al
    jmp     .Lstore
.Ldigit:
    add     $'0', %al
.Lstore:
    mov     %al, (%rsi)
    inc     %rsi
    sub     $4, %ecx
    jns     .Lhex_loop
    movb    $'\n', (%rsi)
    /* write(1, hexbuf, 17) */
    mov     $1, %eax
    mov     $1, %edi
    lea     hexbuf_store(%rip), %rsi
    mov     $17, %edx
    syscall
    ret

    .data
.Lrsp_tag:  .ascii "GEO-RSP "
.Lfs_tag:   .ascii "GEO-FS "
.Lfail_msg: .ascii "GEO-FAIL\n"
    .bss
    .lcomm hexbuf_store, 32
