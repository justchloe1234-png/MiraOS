bits 64

section .multiboot
align 8
header_start:
    dd 0x36d76289      ; multiboot2 magic
    dd 0               ; architecture (0 = i386)
    dd 24              ; total header length in bytes
    dd -(0x36d76289 + 0 + 24) ; checksum

    align 8
    dd 0               ; end tag type
    dd 8               ; end tag size

header_end:

section .bss
align 16
stack_bottom:
    resb 32768
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov rsp, stack_top
    cld
    mov rdi, rax
    mov rsi, rbx
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang
