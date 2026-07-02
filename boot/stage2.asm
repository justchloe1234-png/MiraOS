BITS 32
ORG 0x1000

stage2_removed:
    mov esi, removed_msg
    mov edi, 0xB8000
    call print_string_32
    hlt
    jmp $

print_string_32:
    pusha
.next_char:
    lodsb
    test al, al
    jz .done
    mov [edi], al
    inc edi
    mov byte [edi], 0x07
    inc edi
    jmp .next_char
.done:
    popa
    ret

removed_msg db 'Stage2 removed - Stage1 loads kernel directly', 0

times 2048-($-$$) db 0