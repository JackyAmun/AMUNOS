; boot.asm — AMUNOS stage2: loads the kernel after the stage1/stage2 reserve.

[BITS 16]
%ifdef STAGE2_BUILD
org 0x8000
%else
org 0x7C00
%endif

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
bpb_sec_per_track:  dw 36           ; generated floppy is 2.88 MB
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
    ; MBR/BIOS 可能在跳转前重新开启 IF。保护模式切换前必须屏蔽 IRQ，
    ; 否则定时器会把实模式 IVT 当作保护模式 IDT，直接跳到无效地址。
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; 二阶段读取内核 384 扇: AH=41h 有扩展读(硬盘)→ AH=42h 每次 1 扇
    ; (兼容有边界/批量长度限制的 BIOS); 无(软盘)→ 02h 分块 CHS
    ; (跨道读会失败)。失败复位重试 3 次(公共 int13_retry)。保存真实 DL。
    mov [bpb_drive_num], dl
    ; Visible breadcrumb for the second-stage loader.  Preserve DL first;
    ; BIOS video services may modify it.
    mov byte [0x7B11], '2'
    mov ax, 0xB800
    mov es, ax
    mov byte [es:2], '2'
    mov byte [es:3], 0x0E
    xor ax, ax
    mov es, ax
    mov dl, [bpb_drive_num]
    ; A floppy must use CHS even if a BIOS advertises an EDD extension.
    ; This avoids VMware virtual-floppy BIOS paths that return bad AH=42 data.
    cmp dl, 0x80
    jb .no_ext
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .no_ext
    mov si, dap
    mov eax, [0x7C1C]              ; stage1 BPB hidden sectors (offset 28)
    add eax, 5                    ; stage1 + stage2(4) 后的第一个内核扇区
    mov [dap_lba], eax             ; 分区内核 LBA 1
    mov dl, [bpb_drive_num]
    mov ah, 0x42
    call int13_retry
    mov word [next_segment], 0x1020 ; 下一扇的目标段 (内核物理地址 0x10000)
    mov word [remaining], 383      ; 分区内扇区 2..384
.stage2:
    mov cx, [next_segment]
    mov si, dap
    mov [si+6], cx
    inc word [dap_lba]
    mov dl, [bpb_drive_num]
    mov ah, 0x42
    call int13_retry
    add word [next_segment], 0x0020
    dec word [remaining]
    jnz .stage2
    jmp .load_ok

    ; CHS 分块路径: cnt=SPT-sect0 道对齐, 段增量 cnt*32; bp=当前LBA
.no_ext:
    ; Reset the virtual FDC before entering the CHS path.
    xor ax, ax
    mov dl, [bpb_drive_num]
    int 0x13
    mov bp, 5                      ; stage1 + stage2(4)
    mov si, 0x1000                 ; 内核目标段 (避免覆盖二阶段)
.chs_loop:
    mov ax, bp
    cmp ax, 389
    jae .load_ok
    xor dx, dx
    div word [bpb_sec_per_track]   ; ax=track, dx=sect0(0-based)
    push ax
    push dx
    mov di, [bpb_sec_per_track]
    sub di, dx                     ; cnt = SPT-sect0 (道对齐, 跨道读会失败)
    mov ax, 389
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

    ; Mark that stage2 finished loading the kernel image.
    mov byte [0x7B12], 'K'
    mov ax, 0xB800
    mov es, ax
    mov byte [es:4], 'K'
    mov byte [es:5], 0x0E
    xor ax, ax
    mov es, ax

    ; VBE is opt-in.  VMware may report a nominal mode while exposing an
    ; unusable linear framebuffer; the default path stays on real VGA text.
    ; A boot sector marked "VBE!" can explicitly request the renderer.
    cmp dword [0x7DF0], 0x21454256       ; boot sector marker: "VBE!"
    jne .vbe_done

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
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, 0x0111
    mov di, 0x1600
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov esi, 0x1600
    mov ax, [esi]
    and ax, 0x0081                 ; supported mode + linear framebuffer
    cmp ax, 0x0081
    jne .vbe_done
    cmp word [esi+0x12], 640
    jb .vbe_done
    cmp word [esi+0x14], 480
    jb .vbe_done
    cmp byte [esi+0x19], 16
    jne .vbe_done
    cmp word [esi+0x10], 1280
    jb .vbe_done
    cmp dword [esi+0x28], 0
    je .vbe_done
    mov ax, 0x4F02
    mov bx, 0x4111                 ; bit14 = 线性帧缓冲
    int 0x10
    cmp ax, 0x004F
    jne .vbe_done
    ; Reestablish real-mode data segments and the mode-info pointer after BIOS.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov esi, 0x1600
    mov ax, 0x0600                 ; base/bpl/x/y/bpp → 0x6000 (不覆盖内核)
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
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; BIOS 的 VBE/磁盘服务可能改变 IF，先屏蔽 PIC，避免切换瞬间的 IRQ0
    ; 使用实模式 IVT。内核安装 IDT 和重编程 PIC 后再恢复中断。
    cli
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    xor ax, ax                     ; 保证段基址为 0, lgdt 才能读到 GDT
    mov ds, ax
    mov es, ax

    in al, 0x92                    ; A20
    or al, 2
    out 0x92, al

    cli
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:0x10000

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
next_segment dw 0
remaining  dw 0

align 4
dap: db 0x10, 0
     dw 1
     dw 0, 0x1000
dap_lba: dq 1

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF          ; 0x08 代码段
    dq 0x00CF92000000FFFF          ; 0x10 数据段
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

%ifdef STAGE2_BUILD
    times 2048 - ($ - $$) db 0
%else
    times 510 - ($ - $$) db 0
    dw 0xAA55
%endif
