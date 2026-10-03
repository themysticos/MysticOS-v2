// gdt.h - GDT с ring3-сегментами и TSS.
#ifndef GDT_H
#define GDT_H

// Инициализация GDT (ring0+ring3) и TSS. Ставит ltr.
void gdt_init_all(void);

// Установить TSS.esp0 — стек ядра, куда CPU прыгнет при входе из ring3.
// Вызывается при каждом switch_context (у каждой задачи свой стек ядра).
void gdt_set_esp0(unsigned int esp0);

#endif
