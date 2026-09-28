// pmem.h - физический аллокатор памяти (identity, paging PSE 4 МБ).
// Выдаёт блоки по 4 МБ из верхней RAM — под адресные пространства задач.
#ifndef PMEM_H
#define PMEM_H

#define PMEM_BLOCK 0x400000u          // 4 МБ (совпадает с PSE-страницей)

void pmem_init(void);

// Выделить один блок 4 МБ. Возвращает физический адрес или 0 (нет памяти).
unsigned int pmem_alloc_block(void);

// Освободить блок (адрес, выданный pmem_alloc_block).
void pmem_free_block(unsigned int phys);

#endif
