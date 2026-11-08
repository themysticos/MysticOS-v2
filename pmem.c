// pmem.c - физический аллокатор памяти (блоки 4 МБ, identity).
// Свободная область: от 0x08000000 (выше ELF-кода 0x04000000, стеков задач
// 0x05000000 и буферов окон 0x06000000) до конца RAM (по умолчанию 256 МБ).
#include "pmem.h"

#define PMEM_START 0x08000000u
// ВАЖНО: PMEM_END должен совпадать с реальным объёмом RAM, заданным в QEMU
// (-m). Makefile запускает с -m 512 => верхняя граница 512 МБ (0x20000000).
// Раньше стояло 0x10000000 (256 МБ) — при -m 512 половина памяти простаивала,
// и задачи упирались в лимит ~2 программы.
#define PMEM_END   0x20000000u    // 512 МБ RAM (-m 512)
#define PMEM_BLOCKS ((PMEM_END - PMEM_START) / PMEM_BLOCK)

// 1 = блок занят. 32 блока -> одна 32-битная маска.
static unsigned int g_used = 0;

void pmem_init(void) {
    g_used = 0;   // all free
    // DLL_RESERVE: 0x0C000000..0x0D000000 (4 blocks) for DLL region.
    {
        unsigned int b0 = (0x0C000000u - PMEM_START) / PMEM_BLOCK;
        unsigned int bn = 0x01000000u / PMEM_BLOCK;
        for (unsigned int i = 0; i < bn; i++) g_used |= (1u << (b0 + i));
    }
}

unsigned int pmem_alloc_block(void) {
    for (unsigned int i = 0; i < PMEM_BLOCKS; i++) {
        if (!(g_used & (1u << i))) {
            g_used |= (1u << i);
            return PMEM_START + i * PMEM_BLOCK;
        }
    }
    return 0;   // нет памяти
}

// Выделить N СМЕЖНЫХ блоков (для heap программы, WAD и т.п.).
// Возвращает физ. адрес первого или 0.
unsigned int pmem_alloc_contig(unsigned int nblocks) {
    if (nblocks == 0) return 0;
    for (unsigned int i = 0; i + nblocks <= PMEM_BLOCKS; i++) {
        int ok = 1;
        for (unsigned int j = 0; j < nblocks; j++)
            if (g_used & (1u << (i + j))) { ok = 0; break; }
        if (ok) {
            for (unsigned int j = 0; j < nblocks; j++) g_used |= (1u << (i + j));
            return PMEM_START + i * PMEM_BLOCK;
        }
    }
    return 0;
}

// Освободить регион, выданный pmem_alloc_contig.
void pmem_free_contig(unsigned int phys, unsigned int nblocks) {
    if (phys < PMEM_START || phys >= PMEM_END) return;
    unsigned int b0 = (phys - PMEM_START) / PMEM_BLOCK;
    for (unsigned int j = 0; j < nblocks; j++) g_used &= ~(1u << (b0 + j));
}

void pmem_free_block(unsigned int phys) {
    if (phys < PMEM_START || phys >= PMEM_END) return;
    unsigned int i = (phys - PMEM_START) / PMEM_BLOCK;
    g_used &= ~(1u << i);
}
