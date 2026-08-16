; setjmp.asm — setjmp/longjmp (供 int 0x30 的 exit 与 Ctrl+C 强制终止用)
;
; jmp_buf 布局 (6 个 dword):
;   [0]=ebx [4]=esi [8]=edi [12]=ebp [16]=esp [20]=eip
;
; 用法: cmd_elf 在 call 入口前 setjmp(&prog_jmp);
;       程序 SYS_EXIT 或 force_kill 时 longjmp(&prog_jmp, val) 直接弹回。
[BITS 32]
section .text
global setjmp
global longjmp

setjmp:
    mov eax, [esp+4]      ; eax = jmp_buf*
    mov [eax+0],  ebx
    mov [eax+4],  esi
    mov [eax+8],  edi
    mov [eax+12], ebp
    mov ecx, [esp]        ; 返回地址 (= setjmp 调用点)
    mov [eax+20], ecx     ; 保存 eip
    mov [eax+16], esp     ; 保存 esp (指向返回地址, longjmp 时直接 ret 到 eip)
    xor eax, eax          ; setjmp 直接返回 0
    ret

longjmp:
    mov ecx, [esp+4]      ; ecx = jmp_buf*
    mov eax, [esp+8]      ; eax = value (返回值)
    test eax, eax
    jnz .nz
    mov eax, 1            ; longjmp(buf, 0) → 1
.nz:
    mov ebx, [ecx+0]
    mov esi, [ecx+4]
    mov edi, [ecx+8]
    mov ebp, [ecx+12]
    mov esp, [ecx+16]     ; 恢复 esp (指向保存的返回地址)
    jmp dword [ecx+20]    ; 跳到 setjmp 调用点 (eax = value)
