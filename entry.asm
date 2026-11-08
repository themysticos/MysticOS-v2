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
    ; Стек ЯДРА. РАНЬШЕ 0x90000, но bss ядра кончается на ~0x1D10A0
    ; (растёт с 0x10000 вверх) -> 0x90000 был ВНУТРИ данных: стек затирал
    ; глобальные буферы (аудио HDA и др.) при глубоких вызовах (TLS).
    ; Ставим 0x700000: выше bss с запасом ~5 МБ и ниже pmem (0x08000000).
    mov esp, 0x700000

    ; Обнулить .bss. Загрузчик грузит только .text+.data (kernel.bin);
    ; .bss в файле не хранится, но ОБЯЗАНА быть нулём к моменту старта.
    ; Без этого статические буферы (включая rx_desc_buf, net-буферы)
    ; содержат мусор от BIOS/QEMU -> необъяснимые баги.
    extern __bss_start
    extern __bss_end
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

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

; ============================================================
; RING3: НАСТОЯЩИЙ iret в ring3.
; task_enter_ring3 строит iret-кадр на стеке задачи и делает iret.
; Возврата нет — программа уходит в ring3.
;
; void task_enter_ring3(unsigned int entry, unsigned int ustack_top,
;                       unsigned int api)
;
; Раскладка стека задачи после iret (ring3 esp = ustack_top - 8):
;   [esp+0] = 0        (фиктивный return addr: если _start сделает ret —
;                       прыгнет на 0 и получит page fault; программа
;                       ДОЛЖНА завершаться через api->exit())
;   [esp+4] = api      (cdecl: _start(const mystic_api_t* api))
global task_enter_ring3
task_enter_ring3:
    ; cdecl: [esp+4]=entry [esp+8]=ustack_top [esp+12]=api
    mov eax, [esp+4]      ; entry
    mov ecx, [esp+8]      ; U = ustack_top
    mov edx, [esp+12]     ; api

    ; [U-4] = api
    mov [ecx - 4], edx
    ; [U-8] = 0 (фиктивный return addr)
    mov dword [ecx - 8], 0

    ; esp_ring3 после iret = U - 8
    lea ebx, [ecx - 8]

    ; Переключаемся на стек задачи (пока в ring0) и строим iret-кадр.
    ; iret снимает: eip, cs, eflags, esp, ss (5 dword, сверху вниз).
    mov esp, ebx
    push 0x23             ; ss    = ring3 data | RPL=3
    push ebx              ; esp   = U-8
    push 0x202            ; eflags IF=1
    push 0x1B             ; cs    = ring3 code | RPL=3
    push eax              ; eip   = entry
    ; ВАЖНО: iret меняет CS/SS, но НЕ трогает DS/ES/FS/GS! Если оставить
    ; их ring0-селекторами (0x10, DPL=0), то в ring3 ЛЮБОЕ обращение к
    ; памяти через DS даст #GP (err=0) — реальный CPU (WHPX) это проверяет,
    ; а TCG прощает. Грузим ring3-data (0x23) во все сегменты данных.
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iret

; ============ GDT (ring0 + ring3 + TSS) ============
; Перезагружаем GDT из ядра (загрузчик дал только ring0).
; Селекторы: 0x08=ring0 code, 0x10=ring0 data, 0x18=ring3 code,
;            0x20=ring3 data, 0x28=TSS.
global gdt_init
section .data
gdt_start:
    dq 0                                    ; 0x00 null
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF       ; 0x08 ring0 code
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF       ; 0x10 ring0 data
    dw 0xFFFF, 0x0000, 0xFA00, 0x00CF       ; 0x18 ring3 code (DPL=3)
    dw 0xFFFF, 0x0000, 0xF200, 0x00CF       ; 0x20 ring3 data (DPL=3)
    ; TSS descriptor (0x28) — заполним базой/лимитом в gdt_init
    dw 0, 0, 0x8900, 0x0000
gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start
section .text

; void gdt_init(unsigned int tss_base, unsigned int tss_limit)
gdt_init:
    mov eax, [esp+4]        ; tss_base
    mov edx, [esp+8]        ; tss_limit
    ; Заполняем TSS-дескриптор (смещение 0x28 от gdt_start, 8-й байт = 5*8=40)
    mov ecx, gdt_start + 40
    mov [ecx], dx           ; limit low (2 байта)
    shr edx, 16
    and edx, 0x0F
    mov [ecx+6], dl         ; limit high (4 бита, гранулярность 0)
    mov [ecx+2], ax         ; base low
    shr eax, 16
    mov [ecx+4], al         ; base mid
    shr eax, 8
    mov [ecx+7], al         ; base high
    ; Загрузить GDT и перезагрузить сегменты.
    lgdt [gdt_desc]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload
.reload:
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

; ============ SYSCALL (int 0x80) ============
; Вход: eax=номер, ebx/ecx/edx/esi/edi = аргументы. Выход: eax.
; Пока — заглушка (передаёт в C-обработчик syscall_handler).
extern syscall_handler
global isr128
isr128:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

; (MMDCM перехватывает int из V86 через IVT-ловушки + #GP, а не через
;  отдельные IDT-гейты — поэтому заглушки isr10h/isr20h/isr30h не нужны.)

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
