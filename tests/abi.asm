bits 64
section .text
; rdi = function pointer, rsi/rdx/rcx = destination/source/count.
global check_abi:function
check_abi:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov rbx, 0x12345678
    mov rbp, 0x12345679
    mov r12, 0x1234567a
    mov r13, 0x1234567b
    mov r14, 0x1234567c
    mov r15, 0x1234567d
    call rax
    xor eax, eax
    cmp rbx, 0x12345678
    jne .restore
    cmp rbp, 0x12345679
    jne .restore
    cmp r12, 0x1234567a
    jne .restore
    cmp r13, 0x1234567b
    jne .restore
    cmp r14, 0x1234567c
    jne .restore
    cmp r15, 0x1234567d
    jne .restore
    pushfq
    pop rcx
    test ecx, 1 << 10
    jnz .restore
    mov eax, 1
.restore:
    cld
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
