[BITS 16]
org 0x7C00

    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    mov si, 0x7DBE
    mov cx, 4
.find_active:
    cmp byte [si], 0x80
    je .load_partition
    add si, 16
    loop .find_active
    mov si, 0x7DBE
    cmp dword [si+8], 0
    je .fail

.load_partition:
    mov eax, [si+8]
    mov [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .fail
    mov dl, [boot_drive]
    jmp 0x0000:0x7C00

.fail:
    mov si, error_text
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

boot_drive db 0
error_text db 'MBR boot failed',13,10,0
align 4
dap: db 0x10,0
     dw 1
     dw 0x7C00,0
dap_lba: dq 0
times 446-($-$$) db 0
times 64 db 0
dw 0xAA55
