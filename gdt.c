// gdt.c - GDT (ring0+ring3) и TSS для MysticOS 2.
//
// Этап ring3: добавляем непривилегированные сегменты (DPL=3) и TSS,
// который нужен для перехода ring3 -> ring0 (ядро берёт оттуда стек).
// Пока программы работают в ring0 — GDT/TSS просто готовы к будущему.
#include "gdt.h"

// TSS (32-битный, базовый). Минимум, который нужен для переходов:
// ss0/esp0 — стек ядра, куда прыгаем при входе из ring3.
typedef struct {
    unsigned int prev_tss;
    unsigned int esp0;      // стек ядра для ring3->ring0
    unsigned int ss0;       // сегмент стека ядра (0x10)
    unsigned int esp1, ss1, esp2, ss2;
    unsigned int cr3, eip, eflags;
    unsigned int eax, ecx, edx, ebx, esp, ebp, esi, edi;
    unsigned int es, cs, ss, ds, fs, gs;
    unsigned int ldt;
    unsigned short trap;
    unsigned short iomap_base;
} __attribute__((packed)) tss_t;

static tss_t g_tss;

// Адрес стека ядра для ring3->ring0 (совпадает с entry.asm: esp=0x90000).
#define KERNEL_STACK_TOP 0x90000u

// Внешние (entry.asm):
void gdt_init(unsigned int tss_base, unsigned int tss_limit);
extern void tss_flush(void);   // ltr (сделаем в gdt.c как inline)

// Загрузить TSS (ltr) — селектор 0x28.
static inline void load_tss(void) {
    __asm__ volatile ("ltr %%ax" :: "a"(0x28));
}

void gdt_set_esp0(unsigned int esp0) {
    g_tss.esp0 = esp0;
}

void gdt_init_all(void) {
    // Обнулить TSS.
    unsigned char* p = (unsigned char*)&g_tss;
    for (unsigned int i = 0; i < sizeof(g_tss); i++) p[i] = 0;
    g_tss.ss0 = 0x10;               // сегмент данных ядра (ring0)
    g_tss.esp0 = KERNEL_STACK_TOP;  // стек ядра для входа из ring3
    g_tss.iomap_base = sizeof(g_tss);

    // Заполнить GDT (ring3-сегменты + TSS-дескриптор) и загрузить.
    gdt_init((unsigned int)&g_tss, sizeof(g_tss) - 1);
    load_tss();
}
