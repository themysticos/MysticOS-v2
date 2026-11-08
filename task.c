// task.c - планировщик задач (ring0, paging PSE, per-task адресное пространство).
#include "task.h"
#include "paging.h"
#include "pmem.h"
#include "gdt.h"
#include "pe.h"

static task_t tasks[TASK_MAX];
static int cur = -1;
static int next_id = 1;
static int sched_on = 0;      // 0 = вытеснение выключено (безопасный режим)

// Виртуальные адреса задачи (одинаковы для всех — изоляция через PD).
// Код ELF линкуется на 0x04000000 (PDE[16]); стек — 0x00800000 (PDE[2]).
// Буферы окон (0x06000000, PDE[24]) остаются identity во всех PD.
#define TASK_CODE_VIRT  0x04000000u
#define TASK_STACK_VIRT 0x00800000u
#define TASK_STACK_SZ   0x00010000u   // 64 КБ

// Ядро как задача 0: его контекст — текущий (мы уже работаем).

void task_init(void) {
    for (int i = 0; i < TASK_MAX; i++) {
        tasks[i].state = TASK_FREE;
        tasks[i].esp = 0; tasks[i].pd_phys = 0; tasks[i].id = 0;
        tasks[i].want_exit = 0;
        tasks[i].exit_request_code = 0;
        tasks[i].kernel_stack_phys = 0;
        tasks[i].kernel_stack_top = 0;
        tasks[i].code_phys = 0;
        tasks[i].code_blocks = 0;
        tasks[i].stack_phys = 0;
    }
    // Задача 0 = само ядро. Своё адресное пространство — ядровая PD.
    // pd_phys = адрес ядровой PD (НЕ 0!): возврат из программы обязан
    // восстановить ядровое адресное пространство, а не остаться на PD задачи.
    tasks[0].state = TASK_RUN;
    tasks[0].id = 0;
    tasks[0].pd_phys = (unsigned int)paging_kernel_pd();
    tasks[0].entry = 0; tasks[0].arg = 0;
    cur = 0;
    next_id = 1;
    sched_on = 0;            // пока выключено
}

int task_current(void) { return (cur >= 0) ? tasks[cur].id : -1; }

// --- Задачи с собственным кодом (ELF) ---
// Внутренние/ELF задачи используют общий trampoline: он лежит в ядре,
// доступен в любом адресном пространстве (identity-часть PD не меняется).
extern int exec_load_segments(const unsigned char* image, unsigned int size,
                              unsigned int virt_base, unsigned int phys_base);

// PE-загрузчик (pe.c) — для мультизагрузчика (.exe).
extern int pe_load_segments(const unsigned char* image, unsigned int size,
                            unsigned int virt_base, unsigned int phys_base);

// RING3 ШАГ 1 (диагностика). Ассемблерная task_enter_ring3 (entry.asm)
// просто зовёт эту C-функцию. Пока — печатаем аргументы в консоль и
// запускаем программу как раньше (в ring0). Никаких iret.
// put_char/print_string/print_hex — определены в kprint.c.
void put_char(char c);
void print_string(const char* s);
void print_hex(unsigned int v);
extern void task_enter_ring3(unsigned int entry, unsigned int ustack_top,
                             unsigned int api);

void task_enter_ring3_c(unsigned int entry, unsigned int ustack_top,
                        unsigned int api) {
    // ВАЖНО: console_print/put_char пишут ПРЯМО в LFB (не в backbuffer)
    // и при переполнении строки делают console_scroll(), который СДВИГАЕТ
    // ВЕСЬ LFB вверх на 8 строк — рабочий стол «улетает вверх».
    // Поэтому здесь НЕ печатаем в console. Для отладки используем VGA-текст
    // (0xB8000), он не трогает графику.
    volatile unsigned char* vga = (volatile unsigned char*)0xB8000;
    vga[20 * 2] = 'R';
    vga[21 * 2] = (char)('0' + (entry ? 1 : 0));
    vga[22 * 2] = 'T';
    vga[23 * 2] = (char)('0' + (ustack_top ? 1 : 0));
    vga[24 * 2] = 'A';
    vga[25 * 2] = (char)('0' + (api ? 1 : 0));
    (void)entry; (void)ustack_top; (void)api;

    // Запускаем программу как раньше — в ring0.
    task_t* t = &tasks[cur];
    if (t->entry) t->entry(t->arg);
    task_exit(0);
}

static void task_trampoline(void) {
    task_t* t = &tasks[cur];
    task_enter_ring3((unsigned int)t->entry,
                     (unsigned int)t->stack_top,
                     (unsigned int)t->arg);
    // Не должны сюда вернуться: task_enter_ring3_c сделает task_exit.
    for (;;) { __asm__ volatile ("hlt"); }
}

// Создать задачу из ELF: свой физ.блок кода + стек, своя PD.
int task_create_elf(const unsigned char* image, unsigned int size, const void* api) {
    int slot = -1;
    for (int i = 1; i < TASK_MAX; i++) {   // 0 — ядро
        if (tasks[i].state == TASK_FREE) { slot = i; break; }
    }
    if (slot < 0) return -1;

    // Размер региона код+heap зависит от программы:
    //  * DOOM (и вообще крупные ELF) жёстко инициализирует heap на
    //    0x04400000 размером ~28 МБ (libc_init_heap) => нужен регион
    //    в 8 блоков (32 МБ, до 0x06000000), иначе heap попадает в
    //    незамапленную память и программа падает.
    //  * Обычные системные ELF (10–20 КБ) heap не используют вовсе
    //    => 1 блок (4 МБ) с колоссальным запасом, и, что важно, НЕ
    //    страдают от фрагментации (contig(1) не требует смежных пар).
    // Эвристика: образ > 400 КБ — «тяжёлая» программа (DOOM.elf ~579 КБ).
    unsigned int code_blocks = (size > 409600u) ? 8u : 1u;
    unsigned int code_phys   = pmem_alloc_contig(code_blocks);
    unsigned int stack_phys  = pmem_alloc_block();
    unsigned int kstack_phys = pmem_alloc_block();   // стек ЯДРА этой задачи
    if (!code_phys || !stack_phys || !kstack_phys) {
        if (code_phys)   pmem_free_contig(code_phys, code_blocks);
        if (stack_phys)  pmem_free_block(stack_phys);
        if (kstack_phys) pmem_free_block(kstack_phys);
        return -1;
    }

    int entry = exec_load_segments(image, size, TASK_CODE_VIRT, code_phys);
    if (entry < 0) {
        pmem_free_contig(code_phys, code_blocks); pmem_free_block(stack_phys); pmem_free_block(kstack_phys);
        return -1;
    }

    unsigned int pd = paging_task_pd_alloc(code_phys, stack_phys, code_blocks);
    if (!pd) {
        pmem_free_contig(code_phys, code_blocks); pmem_free_block(stack_phys); pmem_free_block(kstack_phys);
        return -1;
    }

    task_t* t = &tasks[slot];
    t->id = next_id++;
    t->state = TASK_READY;
    t->exit_code = 0;
    t->want_exit = 0;
    t->exit_request_code = 0;
    t->pd_phys = pd;
    t->code_phys = code_phys;
    t->code_blocks = code_blocks;   // ELF: 1 (4 МБ) или 8 (32 МБ) для крупных
    t->stack_phys = stack_phys;
    // Стек ядра задачи: kstack_phys .. kstack_phys + 4 МБ (pmem блок).
    // Верх — туда CPU положит iret-кадр при ring3 -> ring0 (int 0x80).
    // 4 МБ блок — избыточно, но pmem работает блоками 4 МБ. Верх
    // подровняем, чтобы был «нормальный» стек-указатель.
    t->kernel_stack_phys = kstack_phys;
    t->kernel_stack_top  = kstack_phys + PMEM_BLOCK - 16;   // -16 — выравн. по 16
    t->entry = (void (*)(void*))entry;
    t->arg = (void*)api;

    unsigned int phys_top = stack_phys + TASK_STACK_SZ;
    unsigned int* sp = (unsigned int*)phys_top;
    *(--sp) = (unsigned int)task_trampoline;
    *(--sp) = 0x00000202;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    unsigned int used = phys_top - (unsigned int)sp;
    t->esp = (TASK_STACK_VIRT + TASK_STACK_SZ) - used;
    t->stack_base = TASK_STACK_VIRT;
    t->stack_top  = TASK_STACK_VIRT + TASK_STACK_SZ;

    return t->id;
}

// Создать задачу из PE-образа (.exe). Логика та же, что у task_create_elf,
// но секции раскладывает pe_load_segments (по RVA + .reloc HIGHLOW).
int task_create_pe(const unsigned char* image, unsigned int size, const void* api) {
    int slot = -1;
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_FREE) { slot = i; break; }
    }
    if (slot < 0) return -1;

    unsigned int code_phys   = pmem_alloc_block();
    unsigned int stack_phys  = pmem_alloc_block();
    unsigned int kstack_phys = pmem_alloc_block();
    if (!code_phys || !stack_phys || !kstack_phys) {
        if (code_phys)   pmem_free_block(code_phys);
        if (stack_phys)  pmem_free_block(stack_phys);
        if (kstack_phys) pmem_free_block(kstack_phys);
        return -1;
    }

    // PE грузится по phys_base = TASK_CODE_VIRT (identity): секции по RVA
    // от этой базы, .reloc сдвигает абсолютные адреса из ImageBase.
    int entry = pe_load_segments(image, size, TASK_CODE_VIRT, code_phys);
    if (entry < 0) {
        pmem_free_block(code_phys); pmem_free_block(stack_phys); pmem_free_block(kstack_phys);
        return -1;
    }

    // Разрешить импорты (.idata): заполнить IAT адресами из DLL.
    // Вызывается ДО старта задачи. Если импортов нет — вернёт 0.
    pe_load_imports(image, size, code_phys);

    unsigned int pd = paging_task_pd_alloc(code_phys, stack_phys, 1);   // PE: 1 блок
    if (!pd) {
        pmem_free_block(code_phys); pmem_free_block(stack_phys); pmem_free_block(kstack_phys);
        return -1;
    }

    task_t* t = &tasks[slot];
    t->id = next_id++;
    t->state = TASK_READY;
    t->exit_code = 0;
    t->want_exit = 0;
    t->exit_request_code = 0;
    t->pd_phys = pd;
    t->code_phys = code_phys;
    t->code_blocks = 1;   // PE: pmem_alloc_block() = 4 МБ
    t->stack_phys = stack_phys;
    t->kernel_stack_phys = kstack_phys;
    t->kernel_stack_top  = kstack_phys + PMEM_BLOCK - 16;
    // entry — RVA точки входа; виртуальный адрес = TASK_CODE_VIRT + RVA.
    t->entry = (void (*)(void*))(TASK_CODE_VIRT + (unsigned int)entry);
    t->arg = (void*)api;

    unsigned int phys_top = stack_phys + TASK_STACK_SZ;
    unsigned int* sp = (unsigned int*)phys_top;
    *(--sp) = (unsigned int)task_trampoline;
    *(--sp) = 0x00000202;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    unsigned int used = phys_top - (unsigned int)sp;
    t->esp = (TASK_STACK_VIRT + TASK_STACK_SZ) - used;
    t->stack_base = TASK_STACK_VIRT;
    t->stack_top  = TASK_STACK_VIRT + TASK_STACK_SZ;

    return t->id;
}

void task_enable(void) { sched_on = 1; }

// Освободить ресурсы мёртвых задач. Зовётся из ядра (не из переключения).
void task_reap(void) {
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_DEAD) {
            if (tasks[i].code_phys)   pmem_free_contig(tasks[i].code_phys, tasks[i].code_blocks ? tasks[i].code_blocks : 1);
            if (tasks[i].stack_phys)  pmem_free_block(tasks[i].stack_phys);
            if (tasks[i].kernel_stack_phys) pmem_free_block(tasks[i].kernel_stack_phys);
            if (tasks[i].pd_phys)     paging_task_pd_free(tasks[i].pd_phys);
            tasks[i].code_phys = tasks[i].stack_phys = tasks[i].pd_phys = 0;
            tasks[i].kernel_stack_phys = 0;
            tasks[i].kernel_stack_top  = 0;
            tasks[i].state = TASK_FREE;
            tasks[i].id = 0;
            tasks[i].want_exit = 0;
            tasks[i].exit_request_code = 0;
        }
    }
}

int task_any_alive(void) {
    for (int i = 1; i < TASK_MAX; i++)
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUN) return 1;
    return 0;
}

// --- Планировщик ---
void task_yield(void) {
    if (!sched_on) return;
    task_tick();
}

void task_tick(void) {
    if (!sched_on) return;
    if (cur < 0) return;
    int next = cur;
    for (int k = 1; k <= TASK_MAX; k++) {
        int i = (cur + k) % TASK_MAX;
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUN) { next = i; break; }
    }
    if (next == cur) return;
    int prev = cur;
    cur = next;
    if (tasks[prev].state == TASK_RUN) tasks[prev].state = TASK_READY;
    tasks[cur].state = TASK_RUN;
    // У каждой задачи свой стек ядра: при следующем int 0x80 CPU
    // прыгнет на TSS.esp0 именно этой задачи. Для задачи 0 (ядро)
    // оставляем дефолтный 0x90000.
    if (tasks[cur].kernel_stack_top)
        gdt_set_esp0(tasks[cur].kernel_stack_top);
    switch_set_cr3(tasks[cur].pd_phys);   // 0 => не менять (ядро)
    switch_context(&tasks[prev].esp, tasks[cur].esp);
}

// Пометить задачу как "должна завершиться на следующем yield".
// Используется, когда ядро закрывает окно, принадлежащее этой задаче.
int task_request_exit(int task_id, int code) {
    if (task_id <= 0) return 0;   // 0 — ядро, не завершаем
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].state != TASK_FREE && tasks[i].id == task_id) {
            tasks[i].want_exit = 1;
            tasks[i].exit_request_code = code;
            return 1;
        }
    }
    return 0;
}

// Текущая задача должна завершиться? Для api_yield_impl.
int task_current_wants_exit(int* out_code) {
    if (cur <= 0) return 0;
    if (!tasks[cur].want_exit) return 0;
    if (out_code) *out_code = tasks[cur].exit_request_code;
    return 1;
}

void task_exit(int code) {
    if (cur <= 0) { for (;;) __asm__ volatile ("hlt"); }   // ядро не выходит
    tasks[cur].exit_code = code;
    tasks[cur].state = TASK_DEAD;
    int next = -1;
    for (int k = 1; k <= TASK_MAX; k++) {
        int i = (cur + k) % TASK_MAX;
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUN) { next = i; break; }
    }
    if (next < 0) next = 0;   // вернуться в ядро
    int prev = cur;
    cur = next;
    tasks[cur].state = TASK_RUN;
    if (tasks[cur].kernel_stack_top)
        gdt_set_esp0(tasks[cur].kernel_stack_top);
    switch_set_cr3(tasks[cur].pd_phys);
    switch_context(&tasks[prev].esp, tasks[cur].esp);
    // Сюда не возвращаемся (кроме ветки возврата в ядро — там свой стек).
}
