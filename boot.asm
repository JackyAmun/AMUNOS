; boot.asm — AMUNOS FAT12 引导扇区: 扇0 引导 / 1-384 内核(192KB, rsvd=385) /
; 385-402 FAT1+2 / 403-416 根目录 / 417+ 数据簇 (A.img 2880 扇 1.44MB)

[BITS 16]
org 0x7C00

    jmp     short boot_code
    nop

bpb_oem:            db "AMUNOS  "
bpb_bytes_per_sec:  dw 512
bpb_sec_per_cluster: db 1
bpb_rsvd_sec:       dw 385          ; 1引导+384内核 = 192KB 上限
bpb_num_fats:       db 2
bpb_root_entries:   dw 224
bpb_total_sec:      dw 2880
bpb_media:          db 0xF0
bpb_sec_per_fat:    dw 9
bpb_sec_per_track:  dw 18           ; CHS 分块路径使用
bpb_heads:          dw 2            ; CHS 分块路径使用
bpb_hidden_sec:     dd 0
bpb_large_sec:      dd 0

bpb_drive_num:      db 0x80        ; 运行时由 BIOS 传入的 DL 覆写
bpb_reserved:       db 0
bpb_ext_sig:        db 0x29
bpb_vol_id:         dd 0x20260729
bpb_vol_label:      db "AMUNOS     "
bpb_fs_type:        db "FAT12   "

boot_code:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; 读内核 384 扇双路径: AH=41h 有扩展读(硬盘)→ 02h 128扇+42h 2×128扇
    ; (SeaBIOS AH=02h 单次>128 报错); 无(软盘)→ 02h 按道对齐分块 CHS
    ; (跨道读会失败)。失败复位重试 3 次(公共 int13_retry)。保存真实 DL。
    mov [bpb_drive_num], dl
    mov dl, [bpb_drive_num]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .no_ext
    mov ax, 0x0800                 ; 第 1 段: AH=02h 128 扇 LBA1-128
    mov es, ax
    xor bx, bx
    mov cx, 0x0002                 ; ch=0, cl=2 (LBA1 → cyl0 head0 sector2)
    xor dh, dh
    mov dl, [bpb_drive_num]
    mov ax, 0x0280
    call int13_retry
    mov si, 0x7E00                 ; 第 2/3 段: AH=42h 2×128 扇 (DAP@0x7E00)
    mov byte [si], 0x10
    mov byte [si+1], 0
    mov word [si+2], 128
    mov word [si+4], 0
    mov dword [si+8], 0
    mov dword [si+12], 0
    mov bp, 129                    ; bp = 当前 LBA
    mov cx, 0x1800                 ; cx = 目标段 0x18000/0x28000
.stage2:
    mov [si+6], cx
    mov [si+8], bp
    mov dl, [bpb_drive_num]
    mov ah, 0x42
    call int13_retry
    add bp, 128
    add cx, 0x1000
    cmp cx, 0x3800
    jb .stage2
    jmp .load_ok

    ; CHS 分块路径: cnt=SPT-sect0 道对齐, 段增量 cnt*32; bp=当前LBA
.no_ext:
    mov bp, 1
    mov si, 0x0800                 ; si = 当前目标段 (512B 对齐, 偏移恒 0)
.chs_loop:
    mov ax, bp
    cmp ax, 385
    jae .load_ok
    xor dx, dx
    div word [bpb_sec_per_track]   ; ax=track, dx=sect0(0-based)
    push ax
    push dx
    mov di, [bpb_sec_per_track]
    sub di, dx                     ; cnt = SPT-sect0 (道对齐, 跨道读会失败)
    mov ax, 385
    sub ax, bp
    cmp di, ax
    jbe .cnt_ok
    mov di, ax
.cnt_ok:
    mov ax, si                     ; 64K 页内剩余扇数钳制 (软盘 FDC 走
    shl ax, 4                      ; ISA DMA, 单次传输不得跨 64KB 边界)
    mov cx, ax
    shr cx, 9
    mov ax, 128
    sub ax, cx
    cmp di, ax
    jbe .cnt_ok2
    mov di, ax
.cnt_ok2:
    mov es, si                     ; 目标 = si:0 (512B 对齐)
    xor bx, bx
    pop cx
    pop ax
    xor dx, dx
    div word [bpb_heads]           ; ax=cyl, dx=head
    mov ch, al                     ; cyl≤79, cl=sect0≤18 无需高位
    mov dh, dl
    inc cl                         ; 1-based sector
    mov dl, [bpb_drive_num]
    mov ax, di
    mov ah, 0x02
    call int13_retry
    mov ax, di
    add bp, ax
    shl ax, 5                      ; 段增量 = cnt*32 (512B 对齐)
    add si, ax
    jmp .chs_loop

.load_ok:

    ; VBE 640x480x16bpp: 参数写 0x1500 传内核 fb_init; 失败静默跳过。
    ; 关键: 先复位 ES/DS=0, 否则 SeaBIOS 把模式信息写进内核区且读回错位。
    xor ax, ax
    mov es, ax
    mov ds, ax
    mov ax, 0x4F00
    mov di, 0x1400
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    mov ax, 0x4F01
    mov cx, 0x0111
    mov di, 0x1600
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    mov ax, 0x4F02
    mov bx, 0x4111                 ; bit14 = 线性帧缓冲
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    mov ax, 0x0150                 ; base/bpl/x/y/bpp → 0x1500
    mov es, ax
    xor di, di
    mov byte [es:di], 0x01
    mov esi, 0x1600
    mov eax, [esi+0x28]
    mov [es:di+2], eax
    mov ax, [esi+0x12]
    mov [es:di+6], ax
    mov ax, [esi+0x14]
    mov [es:di+8], ax
    mov al, [esi+0x19]
    mov [es:di+10], al
    mov ax, [esi+0x10]
    mov [es:di+11], ax
.vbe_done:

    xor ax, ax                     ; 恢复 ES=0 — 不恢复 lgdt 会加载错 GDT
    mov es, ax

    in al, 0x92                    ; A20
    or al, 2
    out 0x92, al

    cli
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:0x8000

; int13_retry: 调前 AH/DL/参数已设好; 失败→复位重试 3 次→挂死; AH 压栈恢复
int13_retry:
    mov byte [retry_cnt], 3
.r:
    int 0x13
    jnc .ok
    dec byte [retry_cnt]
    jz .fail
    push ax
    mov ah, 0
    int 0x13
    pop ax
    jmp .r
.fail:
    mov si, msg_err
    call print
    hlt
    jmp $
.ok:
    ret

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

msg_err  db 'D Err', 13, 10, 0
retry_cnt  db 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF          ; 0x08 代码段
    dq 0x00CF92000000FFFF          ; 0x10 数据段
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55
