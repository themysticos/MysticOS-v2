; switch.asm - переключение контекста задачи (ring0).
[BITS 32]

section .bss
g_switch_cr3: resd 1        ; физ. адрес PD новой задачи (0 = не менять)
section .text

; Установить CR3, который будет загружен при следующем switch_context.
global switch_set_cr3
switch_set_cr3:
    mov eax, [esp + 4]
    mov [g_switch_cr3], eax
    ret

; void switch_context(unsigned int* old_esp, unsigned int new_esp)
; cdecl: [esp+4]=old_esp  [esp+8]=new_esp
global switch_context
switch_context:
    pushfd
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 24]      ; old_esp
    mov edx, [esp + 28]      ; new_esp
    mov [eax], esp

    ; Если задан CR3 — переключить адресное пространство.
    mov ecx, [g_switch_cr3]
    test ecx, ecx
    jz .no_cr3
    mov cr3, ecx
.no_cr3:
    mov esp, edx

    pop edi
    pop esi
    pop ebx
    pop ebp
    popfd
    ret
