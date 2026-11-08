// paging.c - страничная адресация (PSE, 4 МБ страницы).
// Identity-маппинг: виртуальный адрес == физический. Так вся существующая
// система (framebuffer, backbuffer, код ELF, стеки) продолжает работать
// по тем же адресам, но уже через MMU.
#include "paging.h"

// Страничная директория: 1024 записи по 4 байта = 4 КБ. Должна быть
// выровнена на 4 КБ. Одна запись с флагом PS (бит 7) описывает 4 МБ.
#define PDE_PRESENT 0x1
#define PDE_RW      0x2
#define PDE_PS      0x80        // 4 МБ страница
// PDE_USER: разрешить доступ из ring3. БЕЗ него ring3 получит page fault
// на первой же попытке выполнить свою инструкцию (CS DPL=3, PDE — supervisor).
// Ставим ТОЛЬКО на task-страницы (stack+code), не на ядро — это даёт
// настоящую изоляцию: ring3 не может писать в память ядра.
// (Общие страницы — окна, DLL-регион — пока остаются supervisor: чтобы
// ring3 мог рисовать в буфер окна, нужно будет добавить PDE_USER и туда.
// Но это отдельный шаг, а пока проверяем только сам iret.)
#define PDE_USER    0x4

// Разместим PD статически, выровняв на 4 КБ.
static unsigned int g_pd[1024] __attribute__((aligned(4096)));

unsigned int* paging_kernel_pd(void) { return g_pd; }

#define PMAX_TASK_PD 12   // должно быть >= TASK_MAX (см. task.h):
                          // по одному PD (page directory, 4 КБ) на задачу-программу.
                          // Раньше 8 — при 7 открытых программах 8-я не запускалась.
static unsigned int g_task_pd[PMAX_TASK_PD][1024] __attribute__((aligned(4096)));
static unsigned int g_pd_used_mask = 0;

#define PDE_IDX_STACK 2
#define PDE_IDX_CODE 16
#define PDE_IDX_DLL  48    // 0x0C000000 / 4MB = 48 (DLL region)
// Код+heap программы: 8 PDE (32 МБ), 0x04000000..0x06000000, с PDE_USER.
// Раньше был 1 PDE (4 МБ) - мало для Doom (WAD 11 МБ).
#define PDE_CODE_BLOCKS 8

unsigned int paging_task_pd_alloc(unsigned int code_phys, unsigned int stack_phys,
                                  unsigned int code_blocks) {
    int slot = -1;
    for (int i = 0; i < PMAX_TASK_PD; i++) {
        if (!(g_pd_used_mask & (1u << i))) { slot = i; break; }
    }
    if (slot < 0) return 0;
    g_pd_used_mask |= (1u << slot);

    unsigned int* pd = g_task_pd[slot];
    for (int i = 0; i < 1024; i++) pd[i] = g_pd[i];
    // PDE_USER — иначе ring3 не сможет выполнить свою же инструкцию.
    // Шаг 2: только stack+code. Ядру и shared-страницам (окна, DLL) —
    // пока НЕ ставим user: ring3 не должен писать в ядро.
    pd[PDE_IDX_STACK] = stack_phys | PDE_PRESENT | PDE_RW | PDE_USER | PDE_PS;
    // Код+heap: code_blocks PDE (1 или 8), непрерывные физ. блоки от
    // code_phys. Мапим РОВНО столько, сколько выделено — иначе лишние PDE
    // указывали бы на невыделенную/чужую физпамять.
    for (unsigned int b = 0; b < code_blocks; b++)
        pd[PDE_IDX_CODE + b] = (code_phys + b * 0x400000u) | PDE_PRESENT | PDE_RW | PDE_USER | PDE_PS;
    // DLL-регион (0x64000000, PDE[400]) — тоже USER: ring3-программы
    // исполняют код DLL и читают её данные. Иначе #PF (PG 0x05) на EIP=0x64001000.
    pd[PDE_IDX_DLL]   = (PDE_IDX_DLL * 0x400000u) | PDE_PRESENT | PDE_RW | PDE_USER | PDE_PS;
    return (unsigned int)pd;
}

// Выделить PD для V86-задачи: PDE[0] (0..4 МБ) -> DOS-память (dos_phys),
// остальное — копия ядровой PD (identity: ядро, буферы, LFB).
// V86 обращается к 0..1 МБ по физическим адресам seg*16+off, поэтому
// подменяя PDE[0], мы даём DOS-программе «свои» 0..1 МБ, не трогая ядро.
unsigned int paging_v86_pd_alloc(unsigned int dos_phys) {
    int slot = -1;
    for (int i = 0; i < PMAX_TASK_PD; i++) {
        if (!(g_pd_used_mask & (1u << i))) { slot = i; break; }
    }
    if (slot < 0) return 0;
    g_pd_used_mask |= (1u << slot);
    unsigned int* pd = g_task_pd[slot];
    for (int i = 0; i < 1024; i++) pd[i] = g_pd[i];
    // PDE[0]: 0..4 МБ -> DOS-память. RW, present, PS (4 МБ). Без USER:
    // V86 работает с привилегиями ring0 (это не ring3!).
    pd[0] = dos_phys | PDE_PRESENT | PDE_RW | PDE_PS;
    return (unsigned int)pd;
}

void paging_task_pd_free(unsigned int pd_phys) {
    for (int i = 0; i < PMAX_TASK_PD; i++) {
        if ((unsigned int)g_task_pd[i] == pd_phys) { g_pd_used_mask &= ~(1u << i); return; }
    }
}

// Включить/выключить USER-доступ к низким 4 МБ (PDE[0]).
// Нужно на время V86: V86 работает на CPL=3, а низкая память — supervisor.
// Без USER V86 падает #PF на выборке первой же инструкции.
void paging_low_user(int on) {
    if (on) g_pd[0] |= PDE_USER;
    else    g_pd[0] &= ~PDE_USER;
    // Сбросить TLB: перезагрузить CR3.
    unsigned int cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3));
}

void paging_init(void) {
    // Заполняем все 1024 записи: каждая — 4 МБ, identity.
    for (unsigned int i = 0; i < 1024; i++) {
        unsigned int e = (i * 0x400000u) | PDE_PRESENT | PDE_RW | PDE_PS;
        // Верхние 2 ГБ (0x80000000..0xFFFFFFFF) — MMIO (LFB, PCI, APIC).
        // Их НЕЛЬЗЯ кэшировать: иначе записи в LFB "залипают" в кэше
        // и на экран попадают частично (полосы!). На реальном железе это
        // критично (в QEMU кэш не эмулируется, поэтому там не видно).
        // PCD (Page Cache Disable) = bit 4.
        if (i >= 512) e |= 0x10u;   // PCD
        g_pd[i] = e;
    }

    // Загружаем PD в CR3 и включаем PSE + paging.
    paging_load_cr3((unsigned int)g_pd);

    unsigned int cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 4);           // PSE
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4));

    unsigned int cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;         // PG
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
}
