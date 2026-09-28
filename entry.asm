; entry.asm - точка входа MysticOS 2
[BITS 32]

section .text
global _start
extern kernel_main

_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    call kernel_main

    cli
    hlt
    jmp $

; --- Вызов ELF-программы с сохранением точки возврата ---
global g_exec_ret_esp
global g_exec_ret_eip
section .bss
g_exec_ret_esp: resd 1
g_exec_ret_eip: resd 1
section .text

; int exec_call(int (*entry)(void*), void* arg, unsigned int stack_top)
global exec_call
exec_call:
    ; cdecl: [esp+4]=entry [esp+8]=arg [esp+12]=stack_top
    mov [g_exec_ret_esp], esp
    mov esi, [esp]
    mov [g_exec_ret_eip], esi
    mov eax, [esp+4]
    mov ecx, [esp+8]
    mov edx, [esp+12]
    mov esp, edx
    push ecx
    call eax
    ; Программа вернулась нормально: восстановить ядрёный стек и вернуться
    ; к вызвавшему. Без этого голый ret снимал бы мусор со стека программы
    ; и прыгал по случайному адресу ("улёт").
    mov esp, [g_exec_ret_esp]
    ret

; void exec_do_exit(int code) — принудительно завершить программу
global exec_do_exit
exec_do_exit:
    mov eax, [esp+4]
    mov esp, [g_exec_ret_esp]
    ret

; ============ IDT ============
global idt_load
idt_load:
    mov eax, [esp+4]
    lidt [eax]
    ret

; ============ ISR (исключения) ============
extern isr_handler
global isr_common
isr_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call isr_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret

%macro ISR_NOERR 1
    global isr%1
    isr%1:
        push 0
        push %1
        jmp isr_common
%endmacro
%macro ISR_ERR 1
    global isr%1
    isr%1:
        push %1
        jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; ============ IRQ ============
global irq_common
extern irq_handler
irq_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call irq_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret

%macro IRQ 2
    global irq%1
    irq%1:
        push 0
        push %2
        jmp irq_common
%endmacro

IRQ 0,  0x20
IRQ 1,  0x21
IRQ 2,  0x22
IRQ 3,  0x23
IRQ 4,  0x24
IRQ 5,  0x25
IRQ 6,  0x26
IRQ 7,  0x27
IRQ 8,  0x28
IRQ 9,  0x29
IRQ 10, 0x2A
IRQ 11, 0x2B
IRQ 12, 0x2C
IRQ 13, 0x2D
IRQ 14, 0x2E
IRQ 15, 0x2F
