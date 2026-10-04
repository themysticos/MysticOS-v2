; mmdcm_asm.asm - вход/выход из V86 (Virtual 8086) режима.
;
;   тут мы собираем iret-кадр руками и прыгаем в 1985 год.
;   если что-то сломается — я не виноват. наверное.
;
;   iret с VM=1. вот и вся магия. но лучше не трогать.
; mmdcm_enter_v86(pd_phys, cs, ip, ss, sp, ds_es):
;   переключает CR3 на PD V86-задачи, строит V86 iret-кадр (VM=1), iret.
; Назад — через mmdcm_leave_v86 (из обработчика выхода int 20h).
[BITS 32]

section .bss
    g_v86_kernel_esp: resd 1
    g_v86_kernel_cr3: resd 1
    g_v86_save_ebx: resd 1
    g_v86_save_esi: resd 1
    g_v86_save_edi: resd 1
    g_v86_save_ebp: resd 1
section .text

; cdecl: [esp+4]=pd [esp+8]=cs [esp+12]=ip [esp+16]=ss [esp+20]=sp [esp+24]=ds_es
global mmdcm_enter_v86
mmdcm_enter_v86:
    mov [g_v86_kernel_esp], esp     ; запомнить стек ядра для возврата
    mov eax, cr3
    mov [g_v86_kernel_cr3], eax     ; запомнить CR3 ядра
    ; Сохранить callee-saved регистры (cdecl ABI): мы используем их для
    ; аргументов, а вызывающий (C) ожидает их сохранёнными.
    mov [g_v86_save_ebx], ebx
    mov [g_v86_save_esi], esi
    mov [g_v86_save_edi], edi
    mov [g_v86_save_ebp], ebp

    mov eax, [esp + 4]      ; pd_phys
    mov ecx, [esp + 8]      ; cs
    mov edx, [esp + 12]     ; ip
    mov ebx, [esp + 16]     ; ss
    mov esi, [esp + 20]     ; sp
    mov edi, [esp + 24]     ; ds_es

    ; Сменить CR3 только если задан непустой PD. ИНАЧЕ ядро (0x10000, в PDE[0])
    ; исчезнет из адресного пространства -> CPU потеряет исполняемый код.
    test eax, eax
    jz .no_cr3
    mov cr3, eax
.no_cr3:

    ; --- V86 iret-кадр. push (снизу вверх): eip, cs, eflags(VM), esp, ss,
    ;     затем (V86) es, ds, fs, gs. CPU снимет в порядке gs..eip. ---
    push 0                  ; gs
    push 0                  ; fs
    push edi                ; ds
    push edi                ; es
    push ebx                ; ss (V86)
    push esi                ; esp (V86)
    push 0x00020202         ; eflags: IF(0x200) | VM(0x20000)
    push ecx                ; cs (V86)
    push edx                ; ip (V86)
    iret

; Восстановить CR3+esp ядра и вернуться в C.
global mmdcm_leave_v86
mmdcm_leave_v86:
    mov eax, [g_v86_kernel_cr3]
    mov cr3, eax
    ; Восстановить callee-saved регистры, которые испортил enter_v86
    ; (он использовал ebx/esi/edi как аргументы V86). Без этого
    ; возвращающийся C-код получает мусор -> паника.
    mov ebx, [g_v86_save_ebx]
    mov esi, [g_v86_save_esi]
    mov edi, [g_v86_save_edi]
    mov ebp, [g_v86_save_ebp]
    mov esp, [g_v86_kernel_esp]
    ret
