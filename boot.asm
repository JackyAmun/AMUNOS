; boot.asm — AMUNOS FAT12 引导扇区 (512 字节)
;
; 标准 FAT12 布局:
;   偏移 0-2:   jmp 跳过 BPB
;   偏移 3-61:  BIOS Parameter Block + Extended BPB
;   偏移 62-509: 引导代码
;   偏移 510-511: 0xAA55
;
; 磁盘布局 (A.img, 2880 扇区, 1.44MB):
;   扇区 0:       引导扇区
;   扇区 1-104:   内核 (保留区, 105 保留扇区)
;   扇区 105-113: FAT1 (9 扇区)
;   扇区 114-122: FAT2 (9 扇区)
;   扇区 123-136: 根目录 (14 扇区 × 224 条目)
;   扇区 137+:    数据簇

[BITS 16]
org 0x7C00

; ── FAT12 BPB 头部 ──
    jmp     short boot_code    ; EB 3C — 跳过 BPB
    nop                        ; 90 — NOP 填充

; ── BIOS Parameter Block (偏移 3-35) ──
bpb_oem:            db "AMUNOS  "  ; OEM 名称 (8 字节)
bpb_bytes_per_sec:  dw 512         ; 每扇区字节数
bpb_sec_per_cluster: db 1          ; 每簇扇区数
bpb_rsvd_sec:       dw 105          ; 保留扇区数 (1引导+104内核)
bpb_num_fats:       db 2           ; FAT 表份数
bpb_root_entries:   dw 224         ; 根目录条目数
bpb_total_sec:      dw 2880        ; 总扇区数 (1.44MB)
bpb_media:          db 0xF0        ; 介质描述符 (1.44MB 软盘)
bpb_sec_per_fat:    dw 9           ; 每 FAT 扇区数
bpb_sec_per_track:  dw 18          ; 每道扇区数
bpb_heads:          dw 2           ; 磁头数
bpb_hidden_sec:     dd 0           ; 隐藏扇区数
bpb_large_sec:      dd 0           ; 大容量扇区数 (2880 < 65536 用不上)

; ── Extended BPB (偏移 36-61) ──
bpb_drive_num:      db 0x80        ; 驱动器号
bpb_reserved:       db 0           ; 保留
bpb_ext_sig:        db 0x29        ; 扩展引导签名
bpb_vol_id:         dd 0x20260729  ; 卷序列号
bpb_vol_label:      db "AMUNOS     " ; 卷标 (11 字节)
bpb_fs_type:        db "FAT12   "  ; 文件系统类型 (8 字节)

; ── 引导代码 (偏移 62) ──
boot_code:
    ; 1. 初始化段寄存器和栈
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; 2. 显示启动信息
    mov si, msg_boot
    call print

    ; 3. CHS 读取内核 (35 扇区 → 0x8000)
    ;    LBA 1 → CHS: cylinder=0, head=0, sector=2
    ;    S=18, H=2 — 标准 1.44MB 软盘几何
    mov ax, 0x0800        ; 内核加载段 ES=0x0800
    mov es, ax
    xor bx, bx            ; 偏移 = 0

    mov cl, 2             ; 起始扇区 (LBA1 → sector 2)
    mov ch, 0             ; 柱面 0
    mov dh, 0             ; 磁头 0
    mov dl, 0x80          ; 第一硬盘
    mov al, 104           ; 读 104 扇区 (kernel<=52KB)
    mov ah, 0x02
    int 0x13
    jnc .load_ok

    mov si, msg_err
    call print
    hlt
    jmp $

.load_ok:
    mov si, msg_ok
    call print

    ; 4. 开启 A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 5. 关中断, 加载 GDT
    cli
    lgdt [gdt_ptr]

    ; 6. 进入保护模式
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 7. 跳转到内核
    jmp 0x08:0x8000


; ── 子程序 ──
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret


; ── 数据 ──
msg_boot db 'AMUNOS Boot...', 13, 10, 0
msg_ok   db 'Kernel OK', 13, 10, 0
msg_err  db 'Disk Error!', 13, 10, 0

; ── GDT ──
align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF  ; 0x08 代码段
    dq 0x00CF92000000FFFF  ; 0x10 数据段
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ── 引导签名 ──
times 510 - ($ - $$) db 0
dw 0xAA55
