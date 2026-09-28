// pmem.c - физический аллокатор памяти (блоки 4 МБ, identity).
// Свободная область: от 0x08000000 (выше ELF-кода 0x04000000, стеков задач
// 0x05000000 и буферов окон 0x06000000) до конца RAM (по умолчанию 256 МБ).
#include "pmem.h"

#define PMEM_START 0x08000000u
#define PMEM_END   0x10000000u    // 256 МБ RAM (-m 256)
#define PMEM_BLOCKS ((PMEM_END - PMEM_START) / PMEM_BLOCK)

// 1 = блок занят. 32 блока -> одна 32-битная маска.
static unsigned int g_used = 0;

void pmem_init(void) {
    g_used = 0;   // всё свободно
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

void pmem_free_block(unsigned int phys) {
    if (phys < PMEM_START || phys >= PMEM_END) return;
    unsigned int i = (phys - PMEM_START) / PMEM_BLOCK;
    g_used &= ~(1u << i);
}
