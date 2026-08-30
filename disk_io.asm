; disk_io.asm — IDE PIO 磁盘读写 (含超时 + 返回值)
;
; v6.6: 支持 4 盘 — drive_idx bit1 选通道 (0=主 0x1F0, 1=次 0x170),
;       bit0 选主/从 (0=Master 0xE0, 1=Slave 0xF0)。
;       0=A(主盘) 1=B(从盘) 2=C(次主盘) 3=D(次从盘)。
; v6.5.6 修复:
;   B3 磁盘锁: 入口 pushfd+cli / 出口 popfd — 内核有抢占调度
;       (demo_clock_task 等), 并发 PIO 端口操作曾可交错损坏数据。
;       单次 PIO 读/写 <1ms, 关中断可接受。所有磁盘访问都经这两个
;       函数, 锁在这里即全局生效。
;   B7 错误恢复: ERR 置位/超时 → 0x3F6(0x376) 软复位 (SRST 脉冲 +
;       等 BSY 清零) 后自动重试, 共 3 次; 仍失败才返回 -1。
;       原实现一遇 ERR/超时就返回, 驱动器偶发错误即上层当垃圾数据用。

[BITS 32]
section .text
global read_sector_asm
global write_sector_asm
global identify_drive_asm

DISK_TIMEOUT equ 0x100000

; -------------------------------------------------------------------------
; ide_soft_reset: 对 ebx 所指通道 (0x1F0/0x170) 做软复位并等 BSY 清零。
; 内部例程: pusha/popa 保寄存器, 不改返回值槽。调用方须处于 cli 状态。
; -------------------------------------------------------------------------
ide_soft_reset:
    pusha
    mov edx, 0x3F6                 ; 主通道控制寄存器
    cmp ebx, 0x1F0
    je .rs_sel
    mov edx, 0x376                 ; 次通道控制寄存器
.rs_sel:
    mov al, 0x04                   ; SRST=1
    out dx, al
    mov ecx, 0x2000                ; 保持复位 ≥5us (实际远长于此, 无害)
.rs_h1:
    dec ecx
    jnz .rs_h1
    mov al, 0x00                   ; SRST=0, 设备重新校准
    out dx, al
    mov edx, ebx
    add edx, 7                     ; 状态寄存器: 等 BSY 清零
    mov ecx, DISK_TIMEOUT
.rs_wait:
    in al, dx
    test al, 0x80
    jz .rs_done
    dec ecx
    jnz .rs_wait
.rs_done:
    popa
    ret

; -------------------------------------------------------------------------
; 栈布局 (两入口相同):
;   push ebp / mov ebp,esp / pushfd / cli / pushad / sub esp,4
;   [esp]      = 重试计数
;   [esp+32]   = pushad 保存的 EAX (返回值槽; pushfd 使之比旧版 +4)
;   [ebp+8]    = lba   [ebp+12] = buffer   [ebp+16] = drive_idx
; 返回: 0=成功, -1=失败 (重试 3 次后仍超时/错误)
; -------------------------------------------------------------------------
read_sector_asm:
    push ebp
    mov ebp, esp
    pushfd
    cli                            ; B3 磁盘锁: 期间不可被抢占改通道寄存器
    pushad
    sub esp, 4
    mov dword [esp], 3             ; 重试次数

    ; 0. 通道基址: drive_idx bit1 = 1 → 次通道 0x170, 否则主通道 0x1F0
    mov ebx, 0x1F0
    mov ecx, [ebp + 16]
    test ecx, 2
    jz .base_ok_r
    mov ebx, 0x170
.base_ok_r:

.retry_r:
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

    ; 5. 等待 DRQ (ERR 置位 → 复位重试)
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
    mov dword [esp + 32], 0
    jmp .exit_r

.fail_r:
    call ide_soft_reset           ; B7: 软复位后重试 (共 3 次)
    dec dword [esp]
    jnz .retry_r
    mov dword [esp + 32], -1      ; 失败 → EAX = -1

.exit_r:
    add esp, 4
    popad
    popfd                         ; 恢复 IF (B3 配对)
    pop ebp
    ret

; -------------------------------------------------------------------------
; int write_sector_asm(int lba, void* buffer, int drive_idx)
; -------------------------------------------------------------------------
write_sector_asm:
    push ebp
    mov ebp, esp
    pushfd
    cli                            ; B3 磁盘锁
    pushad
    sub esp, 4
    mov dword [esp], 3             ; 重试次数

    ; 0. 通道基址
    mov ebx, 0x1F0
    mov ecx, [ebp + 16]
    test ecx, 2
    jz .base_ok_w
    mov ebx, 0x170
.base_ok_w:

.retry_w:
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

    ; 4. 等待 DRQ (ERR 置位 → 复位重试)
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

    ; 等待写入完成 (BSY=0 且 DRDY=1)
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
    mov dword [esp + 32], 0
    jmp .exit_w

.fail_w:
    call ide_soft_reset           ; B7: 软复位后重试 (共 3 次)
    dec dword [esp]
    jnz .retry_w
    mov dword [esp + 32], -1

.exit_w:
    add esp, 4
    popad
    popfd                         ; 恢复 IF (B3 配对)
    pop ebp
    ret

; -------------------------------------------------------------------------
; int identify_drive_asm(int drive_idx, void* buf512)
; ATA IDENTIFY DEVICE (0xEC): 读回 512 字节设备信息。
;   buf word 27-46 = 型号 (40 字节, 大端字序), word 60-61 = LBA 扇区总数。
; 无盘 (选驱动器后 BSY 永不清 / ERR) → 软复位重试 3 次 → -1。
; 栈布局与 read/write_sector_asm 相同, 返回值槽 [esp+32]。
; -------------------------------------------------------------------------
identify_drive_asm:
    push ebp
    mov ebp, esp
    pushfd
    cli                            ; B3 磁盘锁
    pushad
    sub esp, 4
    mov dword [esp], 3             ; 重试次数

    ; 0. 通道基址
    mov ebx, 0x1F0
    mov ecx, [ebp + 8]
    test ecx, 2
    jz .base_ok_i
    mov ebx, 0x170
.base_ok_i:

.retry_i:
    ; 1. 选择驱动器
    lea edx, [ebx + 6]
    mov al, 0xE0
    test ecx, 1
    jz .master_i
    or al, 0xF0
.master_i:
    out dx, al

    ; 等 BSY 清零
    lea edx, [ebx + 7]
    mov ecx, DISK_TIMEOUT
.wait_bsy_i:
    in al, dx
    test al, 0x80
    jz .bsy_ok_i
    dec ecx
    jnz .wait_bsy_i
    jmp .fail_i
.bsy_ok_i:

    ; 2. 扇区数/LBA 全 0 + IDENTIFY 命令
    lea edx, [ebx + 2]
    xor al, al
    out dx, al
    lea edx, [ebx + 3]
    out dx, al
    lea edx, [ebx + 4]
    out dx, al
    lea edx, [ebx + 5]
    out dx, al
    lea edx, [ebx + 7]
    mov al, 0xEC
    out dx, al

    ; 3. 等 DRQ (ERR → 重试); 无盘时状态 0x00 (DRDY 不置位) 也算无盘
    mov ecx, DISK_TIMEOUT
.wait_drq_i:
    in al, dx
    test al, 0x08
    jnz .do_read_i
    test al, 0x01
    jnz .fail_i
    test al, al
    jz .fail_i
    dec ecx
    jnz .wait_drq_i
    jmp .fail_i

.do_read_i:
    mov edi, [ebp + 12]
    mov ecx, 256
    lea edx, [ebx + 0]
    rep insw
    mov dword [esp + 32], 0
    jmp .exit_i

.fail_i:
    ; 调试: 把失败时的状态寄存器记到 buf[0]
    in al, dx
    mov edi, [ebp + 12]
    mov [edi], al
    call ide_soft_reset
    dec dword [esp]
    jnz .retry_i
    mov dword [esp + 32], -1

.exit_i:
    add esp, 4
    popad
    popfd
    pop ebp
    ret
