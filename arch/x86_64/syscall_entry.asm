global syscall_entry

extern syscall_dispatch

; syscall ABI: rax=num, rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5
; rcx=user rip, r11=user rflags (clobbered by syscall)
; GS base points to syscall_cpu_data: [0]=user_rsp, [8]=kernel_rsp
syscall_entry:
    ; save user rsp, switch to kernel stack
    swapgs
    mov  [gs:0], rsp        ; save user rsp
    mov  rsp, [gs:8]        ; load kernel rsp
    swapgs

    push rcx                ; save user rip
    push r11                ; save user rflags

    ; build syscall_dispatch(num, a1, a2, a3, a4, a5) in SysV order
    mov  r9,  r8            ; a5
    mov  r8,  r10           ; a4
    mov  rcx, rdx           ; a3
    mov  rdx, rsi           ; a2
    mov  rsi, rdi           ; a1
    mov  rdi, rax           ; num
    call syscall_dispatch

    pop  r11
    pop  rcx

    ; restore user rsp
    swapgs
    mov  rsp, [gs:0]
    swapgs

    sysretq
