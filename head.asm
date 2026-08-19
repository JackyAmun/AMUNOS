; head.asm — 32 位内核入口

[BITS 32]
section .text
global kernel_entry
global asm_keyboard_handler
global asm_syscall_handler
global asm_timer_handler
global asm_mouse_handler
global asm_fault_ud, asm_fault_gp, asm_fault_pf, asm_fault_df
extern kmain, keyboard_handler, syscall_handler, timer_handler, mouse_handler, fault_handler

kernel_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    call kmain
    hlt
    jmp $

; ── 键盘中断 ──
asm_keyboard_handler:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    call keyboard_handler
    mov al, 0x20
    out 0x20, al
    pop gs
    pop fs
    pop es
    pop ds
    popad
    iretd

; ── 鼠标中断 (IRQ12 → 向量 0x2C) ──
; 从片 IRQ, 需同时向从片 (0xA0) 和主片 (0x20) 发 EOI
asm_mouse_handler:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    call mouse_handler
    mov al, 0x20
    out 0xA0, al          ; EOI 从片
    out 0x20, al          ; EOI 主片 (级联)
    pop gs
    pop fs
    pop es
    pop ds
    popad
    iretd

; ── 系统调用 (int 0x30) ──
; 传栈帧指针给 syscall_handler(frame):
;   frame[4..11]=pushad(edi,esi,ebp,esp,ebx,edx,ecx,eax), frame[12..14]=eip,cs,eflags
asm_syscall_handler:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, esp
    push eax
    call syscall_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popad
    iretd

; ── 定时器中断 (IRQ0 → 向量 0x20) ──
; 调用 timer_schedule(frame), 返回 next task ctx* 在 EAX
; 若非 0: 从 ctx 恢复寄存器 → 切栈 → iret 进入新任务
;  (新任务的 eip/cs/eflags 保存在其自己的栈上, 由 iret 弹出)
%define T_EDI  28
%define T_ESI  24
%define T_EBP  20
%define T_ESP  16
%define T_EBX  12
%define T_EDX  8
%define T_ECX  4
%define T_EAX  0
extern timer_schedule
asm_timer_handler:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, esp          ; 栈帧基址 (esp 指向 gs)
    push eax              ; 参数 (frame)
    call timer_schedule   ; EAX = next task ctx* (或 0)
    add esp, 4            ; 清参数 → esp 指向 gs
    test eax, eax
    jz .no_switch
    ; ── 切换到新任务: 直接恢复寄存器并切栈 ──
    mov edx, eax              ; edx = 新任务 ctx* (ctx 在 .bss, 切栈后仍有效)
    mov al, 0x20
    out 0x20, al              ; EOI 主片 (先发, 后面不再动 eax 低字节)
    mov edi, [edx+T_EDI]
    mov esi, [edx+T_ESI]
    mov ebp, [edx+T_EBP]
    mov ebx, [edx+T_EBX]
    mov ecx, [edx+T_ECX]
    ; 段寄存器 (内核平面模式统一 0x10)
    mov eax, 0x10
    mov ds, eax
    mov es, eax
    mov fs, eax
    mov gs, eax
    ; 最后恢复 eax/edx 并切到新任务栈
    mov eax, [edx+T_EAX]      ; 恢复 eax
    mov esp, [edx+T_ESP]      ; 切到新任务栈 (指向其 iret 帧)
    mov edx, [edx+T_EDX]      ; 恢复 edx (最后; 源 [edx] 取旧值)
    iretd                     ; 弹 eip/cs/eflags → 进入新任务
.no_switch:
    mov al, 0x20
    out 0x20, al              ; EOI 主片
    pop gs
    pop fs
    pop es
    pop ds
    popad
    iretd

; ── CPU 异常 (push vector → pushad+seg → call C) ──
; 无错误码: UD=6
asm_fault_ud: push 6; jmp fault_noec
; 有错误码: DF=8, GPF=13, PF=14
asm_fault_df: push 8; jmp fault_ec
asm_fault_gp: push 13; jmp fault_ec
asm_fault_pf: push 14; jmp fault_ec

; 有错误码 (栈: vector@esp, errcode, eip, cs, eflags)
; pushad 32B + seg*4 16B = 48B, 所以 vector@esp+48, errcode@52, eip@56, cs@60, eflags@64
fault_ec:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, [esp+48]    ; vector
    mov ebx, [esp+56]    ; eip
    mov ecx, [esp+52]    ; errcode
    mov edx, [esp+64]    ; eflags
    push edx
    push ecx
    push ebx
    push eax
    call fault_handler
    add esp, 16
    cli
    hlt

; 无错误码 (栈: vector@esp, eip, cs, eflags)
; vector@esp+48, eip@esp+52, eflags@esp+60
fault_noec:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, [esp+48]    ; vector
    mov ebx, [esp+52]    ; eip
    mov edx, [esp+60]    ; eflags
    push edx
    push 0               ; errcode
    push ebx
    push eax
    call fault_handler
    add esp, 16
    cli
    hlt
