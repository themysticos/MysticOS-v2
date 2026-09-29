// gdt.h - GDT с ring3-сегментами и TSS.
#ifndef GDT_H
#define GDT_H

// Инициализация GDT (ring0+ring3) и TSS. Ставит ltr.
void gdt_init_all(void);

#endif
