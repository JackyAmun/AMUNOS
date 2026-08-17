; disk_io.asm — IDE PIO 磁盘读写 (含超时 + 返回值)
;
; v6.6: 支持 4 盘 — drive_idx bit1 选通道 (0=主 0x1F0, 1=次 0x170),
;       bit0 选主/从 (0=Master 0xE0, 1=Slave 0xF0)。
;       0=A(主盘) 1=B(从盘) 2=C(次主盘) 3=D(次从盘)。

[BITS 32]
section .text
global read_sector_asm
global write_sector_asm

DISK_TIMEOUT equ 0x100000

; -------------------------------------------------------------------------
; int read_sector_asm(int lba, void* buffer, int drive_idx)
; 返回: 0=成功, -1=失败 (超时/错误)
; pushad 栈序 (低→高): EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX
; 要修改返回值需改 [esp+28] = EAX 的保存位置
; -------------------------------------------------------------------------
read_sector_asm:
    push ebp
    mov ebp, esp
    pushad

    ; 0. 通道基址: drive_idx bit1 = 1 → 次通道 0x170, 否则主通道 0x1F0
    mov ebx, 0x1F0
    mov ecx, [ebp + 16]
    test ecx, 2
    jz .base_ok_r
    mov ebx, 0x170
.base_ok_r:

    ; 1. 选择驱动器: dev reg = base+6, 主/从 = 0xE0/0xF0 (两通道相同)
    mov eax, [ebp + 8]
    lea edx, [ebx + 6]
    shr eax, 24
    and al, 0x0F
    test ecx, 1
    jz .master_r
    or al, 0xF0
    jmp .send_dev_r
.master_r:
    or al, 0xE0
.send_dev_r:
    out dx, al

    ; 等待 BSY 清零
    lea edx, [ebx + 7]
    mov ecx, DISK_TIMEOUT
.wait_bsy_r:
    in al, dx
    test al, 0x80
    jz .bsy_ok_r
    dec ecx
    jnz .wait_bsy_r
    jmp .fail_r
.bsy_ok_r:

    ; 2. 扇区数 = 1
    lea edx, [ebx + 2]
    mov al, 1
    out dx, al

    ; 3. LBA
    mov eax, [ebp + 8]
    lea edx, [ebx + 3]
    out dx, al
    lea edx, [ebx + 4]
    shr eax, 8
    out dx, al
    lea edx, [ebx + 5]
    shr eax, 8
    out dx, al

    ; 4. 读命令
    lea edx, [ebx + 7]
    mov al, 0x20
    out dx, al

    ; 5. 等待 DRQ
    mov ecx, DISK_TIMEOUT
.wait_drq_r:
    in al, dx
    test al, 0x08
    jnz .do_read
    test al, 0x01
    jnz .fail_r
    dec ecx
    jnz .wait_drq_r
    jmp .fail_r

.do_read:
    mov edi, [ebp + 12]    ; buffer
    mov ecx, 256
    lea edx, [ebx + 0]
    rep insw
    ; 成功 → 将返回值 EAX 设为 0
    mov dword [esp + 28], 0
    jmp .exit_r

.fail_r:
    mov dword [esp + 28], -1   ; 失败 → EAX = -1

.exit_r:
    popad
    pop ebp
    ret

; -------------------------------------------------------------------------
; int write_sector_asm(int lba, void* buffer, int drive_idx)
; -------------------------------------------------------------------------
write_sector_asm:
    push ebp
    mov ebp, esp
    pushad

    ; 0. 通道基址
    mov ebx, 0x1F0
    mov ecx, [ebp + 16]
    test ecx, 2
    jz .base_ok_w
    mov ebx, 0x170
.base_ok_w:

    ; 1. 选择驱动器
    mov eax, [ebp + 8]
    lea edx, [ebx + 6]
    shr eax, 24
    and al, 0x0F
    test ecx, 1
    jz .master_w
    or al, 0xF0
    jmp .send_dev_w
.master_w:
    or al, 0xE0
.send_dev_w:
    out dx, al

    ; 等待 BSY
    lea edx, [ebx + 7]
    mov ecx, DISK_TIMEOUT
.wait_bsy_w:
    in al, dx
    test al, 0x80
    jz .bsy_ok_w
    dec ecx
    jnz .wait_bsy_w
    jmp .fail_w
.bsy_ok_w:

    ; 2. 参数
    lea edx, [ebx + 2]
    mov al, 1
    out dx, al
    mov eax, [ebp + 8]
    lea edx, [ebx + 3]
    out dx, al
    lea edx, [ebx + 4]
    shr eax, 8
    out dx, al
    lea edx, [ebx + 5]
    shr eax, 8
    out dx, al

    ; 3. 写命令
    lea edx, [ebx + 7]
    mov al, 0x30
    out dx, al

    ; 4. 等待 DRQ
    mov ecx, DISK_TIMEOUT
.wait_drq_w:
    in al, dx
    test al, 0x08
    jnz .do_write
    test al, 0x01
    jnz .fail_w
    dec ecx
    jnz .wait_drq_w
    jmp .fail_w

.do_write:
    mov esi, [ebp + 12]
    mov ecx, 256
    lea edx, [ebx + 0]
    rep outsw

    ; 等待写入完成
    lea edx, [ebx + 7]
    mov ecx, DISK_TIMEOUT
.wait_done_w:
    in al, dx
    and al, 0xC0
    cmp al, 0x40
    je .ok_w
    dec ecx
    jnz .wait_done_w
    jmp .fail_w

.ok_w:
    mov dword [esp + 28], 0
    jmp .exit_w

.fail_w:
    mov dword [esp + 28], -1

.exit_w:
    popad
    pop ebp
    ret
