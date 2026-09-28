; paging.asm - низкоуровневое управление страничной адресацией.
[BITS 32]

section .text

; void paging_load_cr3(unsigned int pd_phys)
global paging_load_cr3
paging_load_cr3:
    mov eax, [esp + 4]
    mov cr3, eax
    ret
