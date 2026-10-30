/* rcrt1.o: startup for static PIE (ET_DYN with no interpreter).
 *
 * Unlike crt0.o, a static PIE is loaded at a random base, so every
 * R_X86_64_RELATIVE relocation must be applied by the program itself
 * before __mlibc_entry runs (there is no dynamic linker to do it).
 * R_X86_64_IRELATIVE resolvers are called after the RELATIVE pass,
 * exactly like glibc's _dl_relocate_static_pie. Any other reloc type
 * hits ud2 on purpose: silently skipping it would corrupt globals.
 *
 * Also provides the hidden __dso_handle that libgcc/libc destructor
 * machinery expects (glibc puts it in rcrt1.o/Scrt1.o, not crt1.o).
 */
.section .text
.global _start
.type _start, @function
_start:
    xor %ebp, %ebp

    # Load bias via the controlled RELATIVE slot below: at this point no
    # reloc has been applied, so *slot still holds its link-time address
    # while LEA gives the runtime address.
    lea __pie_self(%rip), %rax
    mov (%rax), %rbx
    sub %rbx, %rax              # rax = load bias

    # _DYNAMIC is reachable RIP-relative with no relocation involved.
    lea _DYNAMIC(%rip), %rdi    # rdi = runtime _DYNAMIC
    mov %rax, %rsi              # rsi = bias
    call self_reloc

    mov %rsp, %rdi
    lea main(%rip), %rsi
    call __mlibc_entry

    mov %rax, %rdi
    mov $2, %rax
    syscall
    hlt

# self_reloc: rdi = runtime _DYNAMIC, rsi = load bias.
.type self_reloc, @function
self_reloc:
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $8, %rsp                # keep 16-byte alignment for resolver calls

    mov %rsi, %r15              # r15 = bias
    xor %r12, %r12              # r12 = RELA table (runtime)
    xor %r13, %r13              # r13 = RELASZ
    mov $24, %r14               # r14 = RELAENT (Elf64_Rela is 24 bytes)

    # Scan _DYNAMIC for RELA/RELASZ/RELAENT (d_ptr values are link-time
    # RVAs in an ET_DYN, hence + bias).
.Lscan:
    mov (%rdi), %rax            # d_tag
    test %rax, %rax
    jz .Lhave                 # DT_NULL
    cmp $7, %rax                # DT_RELA
    je .Lrela
    cmp $8, %rax                # DT_RELASZ
    je .Lrelasz
    cmp $9, %rax                # DT_RELAENT
    je .Lrelaent
    jmp .Lnext
.Lrela:
    mov 8(%rdi), %r12
    jmp .Lnext
.Lrelasz:
    mov 8(%rdi), %r13
    jmp .Lnext
.Lrelaent:
    mov 8(%rdi), %r14
    jmp .Lnext
.Lnext:
    add $16, %rdi
    jmp .Lscan
.Lhave:
    add %r15, %r12              # runtime RELA
    test %r13, %r13
    jz .Ldone

    # Walk [rela, rela+relasz) in relaent steps. rbx = cursor (callee-saved,
    # so resolver calls preserve it); rax/rdx/r8-r11 are recomputed or
    # dead across each resolver call.
    mov %r12, %rbx
    lea (%r12,%r13), %r13       # r13 = end
.Lloop:
    cmp %r13, %rbx
    jae .Ldone
    mov (%rbx), %rax            # r_offset (link-time VA)
    mov 8(%rbx), %rdx           # r_info
    mov 16(%rbx), %r8           # r_addend
    mov %edx, %r9d              # r_type = info & 0xffffffff
    cmp $8, %r9d                # R_X86_64_RELATIVE
    je .Lrelative
    cmp $42, %r9d               # R_X86_64_IRELATIVE
    je .Liresolve
    ud2                         # unknown static-pie reloc: fail loudly
.Lrelative:
    lea (%rax,%r15), %r10       # loc = offset + bias
    lea (%r8,%r15), %r11        # val = addend + bias
    mov %r11, (%r10)
    jmp .Lstep
.Liresolve:
    lea (%r8,%r15), %r10        # resolver = addend + bias
    call *%r10
    mov (%rbx), %r9             # r_offset (reload: call clobbered rax/rdx/r8)
    lea (%r9,%r15), %r10
    mov %rax, (%r10)            # *loc = resolver()
.Lstep:
    add %r14, %rbx
    jmp .Lloop
.Ldone:
    add $8, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    ret

.section .data
.hidden __pie_self
.globl __pie_self
.type __pie_self, @object
__pie_self:
    .quad __pie_self            # R_X86_64_RELATIVE; pre-reloc content = link-time addr

.hidden __dso_handle
.globl __dso_handle
.type __dso_handle, @object
__dso_handle:
    .quad 0
