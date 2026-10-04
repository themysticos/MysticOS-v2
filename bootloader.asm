; bootloader.asm - MysticOS 2, сектор 0.
; Задача: загрузить stage2 (секторы 1..16) на 0x8000 и прыгнуть туда.
; Вся логика (VBE, диагностика, ядро) — в stage2.asm (не 512 байт).
[BITS 16]
[ORG 0x7C00]

start:
    jmp short boot_code
    nop
    times 62 - ($ - $$) db 0

boot_code:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    ; Загрузить stage2: секторы 1..16 (16 секторов) на 0x8000.
    ; CHS: сектор 1 (после MBR) = сектор 2 по счёту BIOS (1-based).
    mov word [stg_sectors], 16
    mov byte [chs_sector], 2   ; первый сектор stage2 (CHS 1-based)
    mov byte [chs_head], 0
    mov byte [chs_cyl], 0
    mov bx, 0x0800             ; ES:BX = 0x8000 (es=0x800, bx=0)
    mov es, bx
    xor bx, bx

stg_loop:
    cmp word [stg_sectors], 0
    je stg_done
    mov ah, 0x02
    mov al, 1
    mov ch, [chs_cyl]
    mov cl, [chs_sector]
    mov dh, [chs_head]
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    add bx, 512
    jnc .no_wrap
    mov ax, es
    add ax, 0x1000
    mov es, ax
.no_wrap:
    dec word [stg_sectors]
    inc byte [chs_sector]
    cmp byte [chs_sector], 64
    jb stg_loop
    mov byte [chs_sector], 1
    inc byte [chs_head]
    cmp byte [chs_head], 32
    jb stg_loop
    mov byte [chs_head], 0
    inc byte [chs_cyl]
    jmp stg_loop

stg_done:
    ; Передать загрузочный диск в stage2 (dl) и прыгнуть.
    mov dl, [boot_drive]
    jmp 0x0000:0x8000

disk_error:
    mov si, msg_err
    call print_str
    jmp $

print_str:
    mov ah, 0x0E
.l:
    lodsb
    cmp al, 0
    je .d
    int 0x10
    jmp .l
.d: ret

boot_drive  db 0
stg_sectors dw 0
chs_sector  db 2
chs_head    db 0
chs_cyl     db 0
msg_err db 'Stage2 load error!', 0

times 510 - ($ - $$) db 0
dw 0xAA55
