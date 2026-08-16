; crt0.asm — AMUNOS 用户程序启动桩 (cdecl, 链接 libc.a)
;
; 内核 cmd_elf 在 call 入口前, 于固定地址 0x1F0000 布置 argv 块:
;   [0x1F0000]  = argc (dword)
;   [0x1F0004]  = argv[0..argc-1] (char* 数组, 后跟 NULL)
;   (字符串数据紧随其后, 由内核写入)
;
; _start: 读 argc/argv → call main(argc, argv) → SYS_EXIT(返回值)
[BITS 32]
section .text
global _start
extern main

_start:
    mov esi, 0x1F0000        ; argv 块基址
    mov eax, [esi]           ; argc
    lea ecx, [esi + 4]       ; argv (指向指针数组)
    push ecx                 ; 第二参: argv
    push eax                 ; 第一参: argc
    call main
    add esp, 8
    mov ebx, eax             ; 退出码 = main 返回值
    mov eax, 13              ; SYS_EXIT
    int 0x30
.hang:
    jmp .hang
