// paging.h - страничная адресация (PSE, 4 МБ страницы).
// Этап 1: identity-маппинг всей физической памяти (virt == phys).
// Система работает как раньше, но через MMU — база для per-task адресных
// пространств (переключение CR3).
#ifndef PAGING_H
#define PAGING_H

// Включить paging с identity-картой всех 4 ГБ (4 МБ страницы).
void paging_init(void);

// Физический адрес страницы-директории ядра (для клонирования в задачи).
unsigned int* paging_kernel_pd(void);

// Загрузить CR3 (переключить адресное пространство). Реализация в paging.asm.
extern void paging_load_cr3(unsigned int pd_phys);

// Создать PD задачи: копия ядра + подстановка физ. блоков кода (virt 0x04000000)
// и стека (virt 0x00800000). Возвращает физ. адрес PD (для CR3) или 0.
unsigned int paging_task_pd_alloc(unsigned int code_phys, unsigned int stack_phys);
void paging_task_pd_free(unsigned int pd_phys);

#endif
