; boot_helpers.asm - UEFI long mode -> ядро (long mode).
; БЕЗ перехода в 32-бит. Заменяет switch32.asm.
; PIC-совместимо, без абсолютных релокаций в .data.
[BITS 64]
global uefi_jump64
section .text
uefi_jump64:
    ; rcx = entry (MS x64 ABI: первый аргумент в rcx).
    cli
    mov rbx, rcx                ; сохранить точку входа

    ; Загрузить нашу GDT (2 + 8 байт: limit, base).
    lea rax, [rel gdt64]
    sub rsp, 16
    mov word [rsp], (gdt64_end - gdt64 - 1)
    mov [rsp+2], rax
    lgdt [rsp]
    add rsp, 16

    ; Перезагрузить CS на 0x08 (64-бит код) через far return.
    ; В long mode retfq читает 64-битный RIP, но CS берёт из стека.
    lea rax, [rel reload_cs]
    push qword 0x08
    push rax
    retfq
reload_cs:
    ; Сегменты данных: 0x10 (flat 64-бит data).
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Свой стек ядра. Ядро само перенастроит под long mode.
    mov rsp, 0x90000
    mov rbp, rsp

    ; Прыжок в ядро (остаёмся в long mode).
    jmp rbx

section .data
align 16
gdt64:
    dq 0                        ; null
    ; 0x08: 64-бит code, ring0, present, DPL0
    dw 0x0000, 0x0000
    db 0x00, 0x9A, 0x20, 0x00
    ; 0x10: 64-бит data, ring0, present, DPL0
    dw 0x0000, 0x0000
    db 0x00, 0x92, 0x00, 0x00
gdt64_end:
