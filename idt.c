// idt.c - таблица прерываний, исключения и регистрация IRQ-гейтов
#include "idt.h"
#include "console.h"
#include "vbe.h"

static idt_entry_t g_idt[256];
static idt_ptr_t   g_idt_ptr;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

static void idt_set_gate(int n, unsigned int handler) {
    g_idt[n].base_low  = handler & 0xFFFF;
    g_idt[n].base_high = (handler >> 16) & 0xFFFF;
    g_idt[n].selector  = 0x08;
    g_idt[n].always0   = 0;
    g_idt[n].flags     = 0x8E;
}

void idt_set_gate_extern(int n, unsigned int handler) { idt_set_gate(n, handler); }

static const char* exc_name(unsigned int n) {
    switch (n) {
        case 0:  return "Divide-by-zero";
        case 6:  return "Invalid opcode";
        case 8:  return "Double fault";
        case 13: return "General protection fault";
        case 14: return "Page fault";
        default: return "Exception";
    }
}

void isr_handler(registers_t* r) {
    console_set_color(FB_RGB(0xFF,0x44,0x44), FB_RGB(0,0,0));
    console_print("\n=== KERNEL PANIC ===\n");
    console_print("Exception: ");
    console_print(exc_name(r->int_no));
    console_print(" (vector ");
    console_print_int((int)r->int_no);
    console_print(")\n  EIP: ");
    console_print_hex(r->eip);
    console_print("\nSystem halted.\n");
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void idt_init(void) {
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (unsigned int)&g_idt;
    unsigned char* p = (unsigned char*)g_idt;
    for (unsigned int i = 0; i < sizeof(g_idt); i++) p[i] = 0;

    idt_set_gate(0,  (unsigned int)isr0);
    idt_set_gate(1,  (unsigned int)isr1);
    idt_set_gate(2,  (unsigned int)isr2);
    idt_set_gate(3,  (unsigned int)isr3);
    idt_set_gate(4,  (unsigned int)isr4);
    idt_set_gate(5,  (unsigned int)isr5);
    idt_set_gate(6,  (unsigned int)isr6);
    idt_set_gate(7,  (unsigned int)isr7);
    idt_set_gate(8,  (unsigned int)isr8);
    idt_set_gate(9,  (unsigned int)isr9);
    idt_set_gate(10, (unsigned int)isr10);
    idt_set_gate(11, (unsigned int)isr11);
    idt_set_gate(12, (unsigned int)isr12);
    idt_set_gate(13, (unsigned int)isr13);
    idt_set_gate(14, (unsigned int)isr14);
    idt_set_gate(15, (unsigned int)isr15);
    idt_set_gate(16, (unsigned int)isr16);
    idt_set_gate(17, (unsigned int)isr17);
    idt_set_gate(18, (unsigned int)isr18);
    idt_set_gate(19, (unsigned int)isr19);
    idt_set_gate(20, (unsigned int)isr20);
    idt_set_gate(21, (unsigned int)isr21);
    idt_set_gate(22, (unsigned int)isr22);
    idt_set_gate(23, (unsigned int)isr23);
    idt_set_gate(24, (unsigned int)isr24);
    idt_set_gate(25, (unsigned int)isr25);
    idt_set_gate(26, (unsigned int)isr26);
    idt_set_gate(27, (unsigned int)isr27);
    idt_set_gate(28, (unsigned int)isr28);
    idt_set_gate(29, (unsigned int)isr29);
    idt_set_gate(30, (unsigned int)isr30);
    idt_set_gate(31, (unsigned int)isr31);

    idt_load((unsigned int)&g_idt_ptr);
}
