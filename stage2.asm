; ============================================================================
; stage2.asm - MysticOS 2 второй этап загрузки.
; ============================================================================
;
; Грузится загрузчиком (сектор 0) на 0x8000. НЕ ОГРАНИЧЕН 512 байтами!
; Здесь: диагностика [OK]/[ERR], выбор VBE-режима (с fallback), загрузка
; ядра, переход в protected mode.
;
; Раскладка образа:
;   сектор 0      - bootloader (512 б)
;   секторы 1..16 - этот stage2
;   секторы 17+   - ядро (kernel.bin)
;
;   (Мы вынесли VBE сюда, потому что в 512 байт всё не влезало,
;    а на реальном железе нужна диагностика на каждый чих.)
[BITS 16]
[ORG 0x8000]

start:
    ; ds=es=0 (реальный режим, адресация с нуля)
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Запомнить загрузочный диск (передан bootloader'ом через dl).
    mov [boot_drive], dl

    mov si, msg_stage2
    call puts

    ; ------------------------------------------------------------
    ; ДИАГНОСТИКА: VBE Controller Info (4F00).
    ; ------------------------------------------------------------
    mov dword [0x7000], 'VBE2'    ; запрос VBE 2.0+
    mov ax, 0x4F00
    mov di, 0x7000
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe
    ; Проверить сигнатуру 'VESA'.
    cmp dword [0x7000], 'VESA'
    jne .no_vbe
    mov si, msg_vbe_ok
    call puts
    jmp vbe_search

.no_vbe:
    mov si, msg_vbe_err
    call puts
    jmp vbe_fail

; ------------------------------------------------------------
; Поиск режима: перебрать список VideoModePtr (seg:off в 0x0E).
; Хотим 800x600, bpp 16/24/32. Предпочитаем LFB, но принимаем и без.
; ------------------------------------------------------------
vbe_search:
    ; Список режимов: храним offset+segment в ПАМЯТИ (BIOS-вызовы портят
    ; bx/si!). Грузим перед каждым чтением.
    mov ax, [0x700E]
    mov [list_off], ax
    mov ax, [0x7010]
    mov [list_seg], ax
    mov word [loop_ctr], 512

.read_mode:
    cmp word [loop_ctr], 0
    je vbe_fail
    dec word [loop_ctr]

    ; Прочитать слово по (list_seg:list_off).
    mov si, [list_off]
    mov bx, [list_seg]
    push ds
    mov ds, bx
    mov ax, [si]
    pop ds
    cmp ax, 0xFFFF                ; конец списка?
    je vbe_fail

    ; 4F01 -> Mode Info в 0x5000 (НЕ 0x8000: там наш код; НЕ 0x6000:
    ; там близко стек sp=0x7C00). 0x5000 — безопасно.
    mov [cur_mode], ax
    mov cx, ax
    mov ax, 0x4F01
    mov di, 0x5000
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    ; --- Фильтр: разрешение 800x600 ---
    cmp word [0x5000 + 0x12], 800
    jne .next_mode
    cmp word [0x5000 + 0x14], 600
    jne .next_mode

    ; --- Проверить, что это графический режим (bit4) ---
    mov ax, [0x5000 + 0x00]
    test ax, 0x0010
    jz .next_mode

    ; --- ДИАГНОСТИКА: показать КАЖДЫЙ 800x600 графический режим ---
    ;     mode=0x.... bpp=.. pitch=.... attr=0x.... LFB=YES/no
    mov si, msg_cand
    call puts
    mov ax, [cur_mode]
    call put_hex16
    mov si, msg_bpp
    call puts
    mov al, [0x5000 + 0x19]
    call put_dec8
    mov si, msg_masks
    call puts
    mov al, [0x5000 + 0x1F]     ; red size
    call put_dec8
    mov al, ','
    call putc
    mov al, [0x5000 + 0x20]     ; red pos
    call put_dec8
    mov al, ' '
    call putc
    mov al, [0x5000 + 0x21]     ; green size
    call put_dec8
    mov al, ','
    call putc
    mov al, [0x5000 + 0x22]     ; green pos
    call put_dec8
    mov al, ' '
    call putc
    mov al, [0x5000 + 0x23]     ; blue size
    call put_dec8
    mov al, ','
    call putc
    mov al, [0x5000 + 0x24]     ; blue pos
    call put_dec8
    mov si, msg_pitch
    call puts
    mov ax, [0x5000 + 0x10]     ; bytes_per_scan_line (banked)
    call put_dec16
    mov si, msg_linpitch
    call puts
    mov ax, [0x5000 + 0x32]     ; lin_bytes_per_scan_line (LFB, VBE3.0)
    call put_dec16
    mov si, msg_attrs
    call puts
    mov ax, [0x5000 + 0x00]
    call put_hex16
    mov si, msg_fbaddr
    call puts
    mov eax, [0x5000 + 0x28]      ; PhysBasePtr (может быть 0 до 4F02!)
    call put_hex32
    mov ax, [0x5000 + 0x00]       ; перезагрузить attrs (put_hex32 портит eax)
    test ax, 0x0080
    jz .cand_nol
    mov si, msg_lfb_yes
    call puts
    jmp .cand_done
.cand_nol:
    mov si, msg_lfb_no
    call puts
.cand_done:
    mov si, msg_nl
    call puts

    ; --- bpp 16/24/32 ---
    mov dl, [0x5000 + 0x19]
    cmp dl, 16
    jb .next_mode
    cmp dl, 32
    ja .next_mode

    ; --- Есть LFB? (bit7 ModeAttributes) ---
    mov ax, [0x5000 + 0x00]
    test ax, 0x0080
    jz .try_no_lfb            ; нет LFB — попробуем всё равно

    ; --- ПАУЗА: ждать клавишу перед 4F02 ---
    mov si, msg_pause
    call puts
    mov ah, 0x00
    int 0x16

    ; --- Попытка с LFB (bit14 = 0x4000) ---
    mov si, msg_try_lfb
    call puts
    mov bx, [cur_mode]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .try_no_lfb
    mov si, msg_lfb_ok
    call puts
    jmp vbe_ok

.try_no_lfb:
    ; --- Попытка БЕЗ LFB (bank switching) ---
    mov si, msg_try_nolfb
    call puts
    mov bx, [cur_mode]
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .next_mode
    mov si, msg_nolfb_ok
    call puts
    jmp vbe_ok

.next_mode:
    add word [list_off], 2      ; двигаем курсор списка (в памяти!)
    jmp .read_mode

; ------------------------------------------------------------
; Режим выбран. Заполнить 0x9000 параметрами для ядра.
;   0x9000: fb_addr | 0x9004: pitch | 0x9006: w | 0x9008: h | 0x900A: bpp
; ------------------------------------------------------------
vbe_ok:
    mov eax, [0x5000 + 0x28]      ; PhysBasePtr
    mov [0x9000], eax
    mov ax, [0x5000 + 0x10]       ; BytesPerScanLine (pitch)
    mov [0x9004], ax
    mov ax, [0x5000 + 0x12]       ; Xres
    mov [0x9006], ax
    mov ax, [0x5000 + 0x14]       ; Yres
    mov [0x9008], ax
    mov al, [0x5000 + 0x19]       ; BitsPerPixel (0x19)
    mov [0x900A], al
    ; Маски каналов (Linux vesafb так делает): red/green/blue pos/size.
    ; ModeInfo offsets: 0x1F=RedSize 0x20=RedPos 0x21=GreenSize 0x22=GreenPos
    ;                   0x23=BlueSize 0x24=BluePos
    mov al, [0x5000 + 0x20]       ; red_pos
    mov [0x900C], al
    mov al, [0x5000 + 0x1F]       ; red_size
    mov [0x900D], al
    mov al, [0x5000 + 0x22]       ; green_pos
    mov [0x900E], al
    mov al, [0x5000 + 0x21]       ; green_size
    mov [0x900F], al
    mov al, [0x5000 + 0x24]       ; blue_pos
    mov [0x9010], al
    mov al, [0x5000 + 0x23]       ; blue_size
    mov [0x9011], al
    mov si, msg_vbe_done
    call puts
    jmp load_kernel

vbe_fail:
    mov dword [0x9000], 0
    mov si, msg_vbe_fail
    call puts
    ; продолжаем без VBE (ядро уйдёт в текст/панику)

; ------------------------------------------------------------
; Загрузка ядра с сектора 17 (LBA 16).
; ------------------------------------------------------------
load_kernel:
    mov si, msg_load_kern
    call puts
    mov word [kern_sectors], 400
    mov byte [chs_sector], 17      ; первый сектор ядра (сектор CHS)
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
    add ax, 0x1000
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
    mov si, msg_kern_ok
    call puts
    in al, 0x92
    or al, 2
    out 0x92, al
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:0x10000

; ------------------------------------------------------------
; Печать строки (SI), BIOS teletype.
; ------------------------------------------------------------
puts:
    mov ah, 0x0E
.l:
    lodsb
    cmp al, 0
    je .d
    push ax
    int 0x10            ; вывод в VGA (BIOS teletype)
    pop ax
    call serial_putc    ; + дубль в COM1 (видно в -serial stdio)
    jmp .l
.d: ret

; --- Вывод одного символа AL (VGA + serial). ---
putc:
    push ax
    mov ah, 0x0E
    int 0x10
    pop ax
    call serial_putc
    ret

; --- Вывести AL как 2 hex-цифры. ---
put_hex8:
    push ax
    push ax
    shr al, 4
    call hex_digit
    call putc
    pop ax
    and al, 0x0F
    call hex_digit
    call putc
    pop ax
    ret
hex_digit:
    cmp al, 10
    jb .d
    add al, 'A' - 10
    ret
.d: add al, '0'
    ret

; --- Вывести AX как 4 hex-цифры. ---
put_hex16:
    push ax
    mov al, ah
    call put_hex8
    pop ax
    call put_hex8
    ret

; --- Вывести AX как десятичное (0..65535). ---
put_dec16:
    push ax
    push bx
    push cx
    push dx
    mov cx, 0
    mov bx, 10
.div:
    xor dx, dx
    div bx              ; ax / 10 -> ax=частное, dx=остаток
    push dx
    inc cx
    test ax, ax
    jnz .div
.prn:
    pop ax
    add al, '0'
    call putc
    loop .prn
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- Вывести EAX как 8 hex-цифр. ---
put_hex32:
    push eax
    shr eax, 16
    call put_hex16
    pop eax
    call put_hex16
    ret

; --- Вывести AL как десятичное (0..255). ---
put_dec8:
    push ax
    mov ah, 0
    call put_dec16
    pop ax
    ret

; Вывести AL в COM1 (0x3F8), без инициализации (BIOS уже настроил).
serial_putc:
    push dx
    push ax
    mov dx, 0x3FD
.w: in al, dx
    test al, 0x20       ; THR empty?
    jz .w
    pop ax
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

disk_error:
    mov si, msg_disk_err
    call puts
    jmp $

; ------------------------------------------------------------
; Данные.
; ------------------------------------------------------------
boot_drive  db 0
kern_sectors dw 0
chs_sector  db 17
chs_head    db 0
chs_cyl     db 0
cur_mode    dw 0
loop_ctr    dw 0
list_off    dw 0
list_seg    dw 0

msg_stage2    db 'MysticOS stage2', 13, 10, 0
msg_diag      db '[DIAG] mode params:', 13, 10, 0
msg_fbaddr    db 'LFB addr=0x', 0
msg_cand      db '[mode] =', 0
msg_bpp       db ' bpp=', 0
msg_masks     db ' RGB(s,p):', 0
msg_pitch     db ' pitch=', 0
msg_linpitch  db ' linpitch=', 0
msg_attrs     db ' attr=0x', 0
msg_lfb_yes   db ' LFB=YES', 0
msg_lfb_no    db ' LFB=no', 0
msg_wh        db '  res=', 0
msg_nl        db 13, 10, 0
msg_vbe_ok    db '[OK] VBE 2.0+ found', 13, 10, 0
msg_vbe_err   db '[ERR] no VBE (4F00 failed)', 13, 10, 0
msg_try_lfb   db '[..] trying 800x600 with LFB...', 13, 10, 0
msg_lfb_ok    db '[OK] LFB mode set', 13, 10, 0
msg_try_nolfb db '[..] trying WITHOUT LFB...', 13, 10, 0
msg_nolfb_ok  db '[OK] mode WITHOUT LFB set', 13, 10, 0
msg_vbe_done  db '[OK] VBE ready, params at 0x9000', 13, 10, 0
msg_vbe_fail  db '[ERR] no 800x600 mode matched!', 13, 10, 0
msg_load_kern db '[..] loading kernel...', 13, 10, 0
msg_kern_ok   db '[OK] kernel loaded, jumping to protected mode', 13, 10, 0
msg_disk_err  db '[ERR] disk read error!', 13, 10, 0
msg_pause     db 'Press any key...', 13, 10, 0

; GDT для перехода в protected mode.
gdt_start:
    dq 0
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF
gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Дополнить до N секторов (запас под рост).
; times (16 * 512) - ($ - $$) db 0
