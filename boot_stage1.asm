; AMUNOS stage 1: keep this sector deliberately small.
[BITS 16]
org 0x7C00

    jmp short boot_code
    nop

bpb_oem:            db "AMUNOS  "
bpb_bytes_per_sec:  dw 512
bpb_sec_per_cluster: db 1
bpb_rsvd_sec:       dw 389          ; stage2(4) + kernel(384) + boot
bpb_num_fats:       db 2
bpb_root_entries:   dw 224
bpb_total_sec:      dw 2880
bpb_media:          db 0xF0
bpb_sec_per_fat:    dw 9
bpb_sec_per_track:  dw 18
bpb_heads:          dw 2
bpb_hidden_sec:     dd 0
bpb_large_sec:      dd 0
bpb_drive_num:      db 0x00          ; standard floppy drive number; BIOS DL replaces it at boot
bpb_reserved:       db 0
bpb_ext_sig:        db 0x29
bpb_vol_id:         dd 0x20261004
bpb_vol_label:      db "AMUNOS     "
bpb_fs_type:        db "FAT12   "

boot_code:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [bpb_drive_num], dl
    mov eax, [bpb_hidden_sec]
    mov [0x7B00], eax             ; stage2 handoff: volume LBA
    mov dl, [bpb_drive_num]

    ; Hard disks use EDD; floppy fallback uses the known 1.44MB geometry.
    ; Some IDE BIOS paths give an unreliable AH=41h capability result, so
    ; issue the actual AH=42h read directly and use CF as the decision point.
    ; VMware may advertise/partially implement EDD for a virtual floppy;
    ; force the conventional CHS path for DL<80h for reliable booting.
    cmp dl, 0x80
    jb .chs
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, dap
    mov eax, [0x7B00]
    inc eax
    mov [dap_lba], eax
    mov dl, [bpb_drive_num]
    mov ah, 0x42
    int 0x13
    jc .chs
    xor ax, ax
    mov ds, ax
    mov es, ax
    jmp .loaded

.chs:
    ; Reset the floppy controller before CHS reads.  VMware can leave the
    ; virtual FDC busy after BIOS probing, causing an otherwise valid image
    ; to stop with only the firmware cursor visible.
    xor ax, ax
    mov dl, [bpb_drive_num]
    int 0x13
    mov ax, 0x0800
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 4
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [bpb_drive_num]
    int 0x13
    jc .fail

.loaded:
    mov ax, 0xB800
    mov es, ax
    mov byte [es:0], '1'
    mov byte [es:1], 0x0E
    xor ax, ax
    mov es, ax
    mov dl, [bpb_drive_num]
    jmp 0x0800:0x0000

.fail:
    mov si, msg_err
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

msg_err db 'S1 Err', 13, 10, 0
align 4
dap: db 0x10, 0
     dw 4
     dw 0, 0x0800
dap_lba: dq 1

times 510 - ($ - $$) db 0
dw 0xAA55
