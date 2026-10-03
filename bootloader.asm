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

    ; --- VBE: перебрать режимы по убыванию, взять первый с LFB ---
    ; Таблица режимов. Каждый: 2 байта номер, 2 байта "BPP check":
    ;   0 = не проверять BPP (доверяем BIOS), 8/16/24 = требуемый BPP.
    ; Терминатор: 0xFFFF.
    mov dword [0x9000], 0
    mov si, vbe_modes
.vbe_try:
    mov cx, [si]
    cmp cx, 0xFFFF
    je vbe_fail
    add si, 2
    mov bx, cx

    ; 1) Получить инфу о режиме (0x4F01).
    mov ax, 0x4F01
    mov di, 0x8000
    push si
    push bx
    int 0x10
    pop bx
    pop si
    cmp ax, 0x004F
    jne .vbe_next              ; режим не поддерживается

    ; 2) Проверить ModeAttributes: бит 7 (0x80) = LinearFramebuffer доступен.
    mov ax, [0x8000 + 0x00]
    test ax, 0x0080
    jz .vbe_next               ; LFB недоступен

    ; 3) Проверить BPP (если указан):
    ;    MemoryModel (offset 0x1B) = 4 (packed pixel) или 6 (direct color).
    ;    BitsPerPixel (offset 0x19).
    ;    Простой случай: сравнить с ожидаемым, если в таблице задан.

    ; 4) Установить режим (0x4F02). Старший бит BX = использовать LFB.
    mov ax, 0x4F02
    or bx, 0x4000              ; bit 14 = LinearFramebuffer
    int 0x10
    cmp ax, 0x004F
    jne .vbe_next

    ; 5) Записать параметры в 0x9000:
    ;    +0x00: PhysBasePtr (dword)
    ;    +0x04: BytesPerScanLine (word, pitch)
    ;    +0x06: XResolution (word)
    ;    +0x08: YResolution (word)
    ;    +0x0A: BitsPerPixel (byte)
    mov eax, [0x8000 + 0x28]
    mov [0x9000], eax
    mov ax, [0x8000 + 0x10]
    mov [0x9004], ax
    mov ax, [0x8000 + 0x12]
    mov [0x9006], ax
    mov ax, [0x8000 + 0x14]
    mov [0x9008], ax
    mov al, [0x8000 + 0x19]
    mov [0x900A], al

    mov al, 'V'
    call putc_bios
    jmp vbe_done

.vbe_next:
    jmp .vbe_try

vbe_fail:
    mov al, 'v'
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

; Таблица VBE-режимов, по убыванию предпочтения.
; Первый, для которого BIOS отдаст LFB, будет использован.
; 0xFFFF — терминатор.
vbe_modes:
    dw 0x115
    dw 0x11B
    dw 0x11A
    dw 0x118
    dw 0x11D
    dw 0x14A
    dw 0x112
    dw 0x11C
    dw 0x14B
    dw 0x160
    dw 0x165
    dw 0xFFFF

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
