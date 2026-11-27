bits 64
default rel
section .text

; SysV: rdi = output, rsi = input, rdx = word count.
; Exact aliasing or disjoint buffers. xmm1 retains the preceding input lane.
global surco_delta_sse2:function hidden
surco_delta_sse2:
    xor eax, eax
    pxor xmm1, xmm1
.vector:
    cmp rdx, 4
    jb .tail
    movdqu xmm0, [rsi]
    movdqa xmm2, xmm0
    pslldq xmm2, 4
    por xmm2, xmm1
    movdqa xmm1, xmm0
    psrldq xmm1, 12
    psubd xmm0, xmm2
    movdqu [rdi], xmm0
    add rsi, 16
    add rdi, 16
    sub rdx, 4
    jmp .vector
.tail:
    movd eax, xmm1
.next:
    test rdx, rdx
    jz .done
    mov ecx, [rsi]
    mov r8d, ecx
    sub ecx, eax
    mov [rdi], ecx
    mov eax, r8d
    add rsi, 4
    add rdi, 4
    dec rdx
    jmp .next
.done:
    ret

; xmm1 broadcasts the previous vector's final prefix into all four lanes.
global surco_prefix_sse2:function hidden
surco_prefix_sse2:
    pxor xmm1, xmm1
.vector:
    cmp rdx, 4
    jb .tail
    movdqu xmm0, [rsi]
    movdqa xmm2, xmm0
    pslldq xmm2, 4
    paddd xmm0, xmm2
    movdqa xmm2, xmm0
    pslldq xmm2, 8
    paddd xmm0, xmm2
    paddd xmm0, xmm1
    pshufd xmm1, xmm0, 0xff
    movdqu [rdi], xmm0
    add rsi, 16
    add rdi, 16
    sub rdx, 4
    jmp .vector
.tail:
    movd eax, xmm1
.next:
    test rdx, rdx
    jz .done
    add eax, [rsi]
    mov [rdi], eax
    add rsi, 4
    add rdi, 4
    dec rdx
    jmp .next
.done:
    ret

; SysV: edi = uncomplemented CRC state, rsi = bytes, rdx = byte count.
; Caller gates this routine on SSE4.2. x86 byte order matches CRC32's qword order.
global surco_crc_sse42:function hidden
surco_crc_sse42:
    mov eax, edi
.vector:
    cmp rdx, 8
    jb .tail
    crc32 rax, qword [rsi]
    add rsi, 8
    sub rdx, 8
    jmp .vector
.tail:
    test rdx, rdx
    jz .done
    crc32 eax, byte [rsi]
    inc rsi
    dec rdx
    jmp .tail
.done:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
