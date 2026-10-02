// task.c - планировщик задач (ring0, paging PSE, per-task адресное пространство).
#include "task.h"
#include "paging.h"
#include "pmem.h"

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

static void task_trampoline(void) {
    // cur указывает на нашу задачу; её entry — виртуальный адрес точки входа.
    task_t* t = &tasks[cur];
    if (t->entry) t->entry(t->arg);
    task_exit(0);
    for (;;) { __asm__ volatile ("hlt"); }
}

// Создать задачу из ELF: свой физ.блок кода + стек, своя PD.
int task_create_elf(const unsigned char* image, unsigned int size, const void* api) {
    int slot = -1;
    for (int i = 1; i < TASK_MAX; i++) {   // 0 — ядро
        if (tasks[i].state == TASK_FREE) { slot = i; break; }
    }
    if (slot < 0) return -1;

    unsigned int code_phys  = pmem_alloc_block();
    unsigned int stack_phys = pmem_alloc_block();
    if (!code_phys || !stack_phys) {
        if (code_phys)  pmem_free_block(code_phys);
        if (stack_phys) pmem_free_block(stack_phys);
        return -1;
    }

    int entry = exec_load_segments(image, size, TASK_CODE_VIRT, code_phys);
    if (entry < 0) {
        pmem_free_block(code_phys); pmem_free_block(stack_phys);
        return -1;
    }

    unsigned int pd = paging_task_pd_alloc(code_phys, stack_phys);
    if (!pd) {
        pmem_free_block(code_phys); pmem_free_block(stack_phys);
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
    t->stack_phys = stack_phys;
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

void task_enable(void) { sched_on = 1; }

// Освободить ресурсы мёртвых задач. Зовётся из ядра (не из переключения).
void task_reap(void) {
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_DEAD) {
            if (tasks[i].code_phys)  pmem_free_block(tasks[i].code_phys);
            if (tasks[i].stack_phys) pmem_free_block(tasks[i].stack_phys);
            if (tasks[i].pd_phys)    paging_task_pd_free(tasks[i].pd_phys);
            tasks[i].code_phys = tasks[i].stack_phys = tasks[i].pd_phys = 0;
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
    switch_set_cr3(tasks[cur].pd_phys);
    switch_context(&tasks[prev].esp, tasks[cur].esp);
    // Сюда не возвращаемся (кроме ветки возврата в ядро — там свой стек).
}
