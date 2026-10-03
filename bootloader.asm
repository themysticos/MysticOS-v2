; bootloader.asm - MysticOS 2: включает VBE 800x600x24, грузит ядро.
; Сектор 0 (MBR): VBE + загрузка kernel.bin с сектора 2, прыжок в ядро.
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

    ; --- ДИАГНОСТИКА: '1' = наши сектор-0 выполняются. ---
    mov al, '1'
    call putc_bios

    ; --- VBE: установить режим 800x600x24 с линейным framebuffer ---
    mov dword [0x9000], 0
    mov ax, 0x4F01
    mov cx, 0x115
    mov di, 0x8000
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail
    mov eax, [0x8000 + 0x28]
    mov [0x9000], eax
    mov ax, [0x8000 + 0x10]
    mov [0x9004], ax
    mov ax, [0x8000 + 0x12]
    mov [0x9006], ax
    mov ax, [0x8000 + 0x14]
    mov [0x9008], ax
    mov al, 'V'           ; маркер: VBE ok
    call putc_bios
    jmp vbe_done
vbe_fail:
    mov al, 'v'           ; маркер: VBE провал (текстовый режим)
    call putc_bios
vbe_done:

    ; --- Загрузка ядра: сектор 2 и далее (LBA 1+) на 0x10000 ---
    mov word [kern_sectors], 400
    mov byte [chs_sector], 2
    mov byte [chs_head], 0
    mov byte [chs_cyl], 0
    mov bx, 0x1000
    mov es, bx
    xor bx, bx

read_loop:
    cmp word [kern_sectors], 0
    je read_done
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
    add ax, 0x1000      ; +64 КБ (0x1000 параграфов), а не 0x20!
    mov es, ax
.no_wrap:
    dec word [kern_sectors]
    inc byte [chs_sector]
    cmp byte [chs_sector], 64
    jb read_loop
    mov byte [chs_sector], 1
    inc byte [chs_head]
    cmp byte [chs_head], 32
    jb read_loop
    mov byte [chs_head], 0
    inc byte [chs_cyl]
    jmp read_loop

read_done:
    mov al, 'K'           ; маркер: ядро прочитано
    call putc_bios
    mov al, 'J'           ; маркер: сейчас прыжок в ядро (до cli!)
    call putc_bios
    in al, 0x92
    or al, 2
    out 0x92, al
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:0x10000

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

; Печать символа (AL) через BIOS teletype. Сохраняет ВСЕ регистры.
putc_bios:
    pusha
    push ds
    push es
    mov ah, 0x0E
    int 0x10
    pop es
    pop ds
    popa
    ret

boot_drive db 0
kern_sectors dw 0
chs_sector db 2
chs_head db 0
chs_cyl db 0
msg_err db 'Disk Error!', 0

gdt_start:
    dq 0
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF
gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55
