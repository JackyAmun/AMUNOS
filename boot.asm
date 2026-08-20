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

    ; ── 4. VBE 图形模式 (v6.8 中文渲染): 640x480x16bpp 线性帧缓冲 ──
    ;    软件渲染器把 0xB8000 文本 + HZK16 汉字画到帧缓冲。
    ;    失败静默跳过 (保留硬件文本模式, 渲染器禁用)。
    ;    帧缓冲参数写到 0x1500 传给内核 fb_init:
    ;      0x1500 flag(1)  0x1502 fb_base(4)  0x1506 w(2)
    ;      0x1508 h(2)  0x150A bpp(1)  0x150B bpl(2)
    ;    关键: 内核加载后 ES=0x0800, 必须先复位 ES/DS=0, 否则 SeaBIOS
    ;    把模式信息写到 0x0800:0x1600=物理0x9600 (内核区内) 且读回错位。
    xor ax, ax
    mov es, ax
    mov ds, ax
    mov ax, 0x4F00          ; VBE 探测
    mov di, 0x1400          ; VBE 信息块缓冲 (512B, 0x1400-0x15FF)
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    mov ax, 0x4F01          ; 读模式信息
    mov cx, 0x0111          ; 640x480x16bpp (标准 VBE 模式)
    mov di, 0x1600          ; 模式信息缓冲 (256B, 0x1600-0x16FF)
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    mov ax, 0x4F02
    mov bx, 0x4111          ; bit14 = 线性帧缓冲
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    ; 模式已置 — 再读一次模式信息, 取 base/bpl/x/y/bpp 存到 0x1500
    mov ax, 0x4F01
    mov cx, 0x0111
    mov di, 0x1600
    int 0x10
    push es
    mov ax, 0x0150
    mov es, ax
    xor di, di
    mov byte [es:di], 0x01            ; flag = VBE ok
    mov ax, 0x0000
    mov ds, ax
    mov esi, 0x1600
    mov eax, [esi+0x28]               ; physical fb base
    mov [es:di+2], eax
    mov ax, [esi+0x12]                ; x res
    mov [es:di+6], ax
    mov ax, [esi+0x14]                ; y res
    mov [es:di+8], ax
    mov al, [esi+0x19]                ; bpp
    mov [es:di+10], al
    mov ax, [esi+0x10]                ; bytes per scanline
    mov [es:di+11], ax
    pop es
.vbe_done:

    ; ── 5. (英文字库内嵌内核 latin_font.h, 不再经 BIOS INT 10h 取 —
    ;        QEMU SeaBIOS 返回错位数据。此处无复制。)

    ; 6. 恢复 DS/ES=0 — 上面 VBE 调用污染了段寄存器, 后续
    ;    lgdt [gdt_ptr] 用 DS 寻址, 不恢复会加载错误 GDT → 远跳转 GP fault
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; 7. 开启 A20
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
