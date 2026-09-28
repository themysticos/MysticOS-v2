// idt.h - таблица прерываний и обработка исключений
#ifndef IDT_H
#define IDT_H

void idt_init(void);

typedef struct {
    unsigned short base_low;
    unsigned short selector;
    unsigned char  always0;
    unsigned char  flags;
    unsigned short base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    unsigned int ds;
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags, useresp, ss;
} __attribute__((packed)) registers_t;

extern void idt_load(unsigned int idt_ptr_addr);
void idt_set_gate_extern(int n, unsigned int handler);

#endif
