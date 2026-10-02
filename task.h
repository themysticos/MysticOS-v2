// task.h - кооперативно-вытесняемые задачи в ring0 (одно адресное пространство).
// Переключение контекста НЕ требует TSS: код и данные — ring0, flat 4 ГБ.
#ifndef TASK_H
#define TASK_H

#define TASK_MAX 8

#define TASK_FREE  0
#define TASK_READY 1
#define TASK_RUN   2
#define TASK_DEAD  3

typedef struct task_s {
    unsigned int esp;        // сохранённый указатель стека
    unsigned int stack_base; // низ стека (виртуальный)
    unsigned int stack_top;  // верх стека (виртуальный)
    int state;               // TASK_*
    int id;                  // pid
    int exit_code;
    void (*entry)(void*);    // точка входа (для внутренних задач)
    void* arg;
    unsigned int pd_phys;    // физ. адрес PD задачи (CR3)
    unsigned int code_phys;  // физ. блок кода (4 МБ)
    unsigned int stack_phys; // физ. блок стека (4 МБ)
    // want_exit: задача должна завершиться на следующем api->yield().
    // Ставится, когда ядро закрывает окно, ПРИНАДЛЕЖАЩЕЕ этой задаче
    // (см. process_events в kernel.c). Раньше был глобальный g_exit_request,
    // который убивал СЛУЧАЙНУЮ задачу (ту, что сейчас зовёт yield), а не ту,
    // чьё окно закрыли.
    int want_exit;
    int exit_request_code;
} task_t;

void task_init(void);                       // инициализация подсистемы
int  task_create(void (*entry)(void*), void* arg);  // новая задача, id или -1
void task_yield(void);                      // добровольно уступить CPU
void task_exit(int code);                   // завершить текущую задачу
int  task_current(void);                    // id текущей задачи (-1)
void task_tick(void);                       // вызывается из IRQ0 (планировщик)

// Низкоуровневое переключение: сохранить текущий esp в *old, загрузить new.
// Реализовано в switch.asm. CR3 грузится из значения switch_set_cr3().
extern void switch_context(unsigned int* old_esp, unsigned int new_esp);
extern void switch_set_cr3(unsigned int cr3);

// Создать задачу из ELF-образа. Возвращает id или -1.
// Выделяет физ. память под код и стек, строит PD, готовит стек для switch.
int task_create_elf(const unsigned char* image, unsigned int size, const void* api);

// Включить кооперативное переключение (sched_on=1).
void task_enable(void);

// Есть ли ещё живая задача-программа (READY/RUN, id>0)?
int  task_any_alive(void);

// Освободить ресурсы завершённых задач (память, PD). Зовётся из ядра.
void task_reap(void);

// Пометить задачу как "должна завершиться на следующем yield".
// Используется, когда ядро закрывает окно, принадлежащее этой задаче.
// 1 при успехе, 0 если задача не найдена.
int task_request_exit(int task_id, int code);

// Текущая задача должна завершиться? Для вызова из api_yield_impl.
int task_current_wants_exit(int* out_code);

#endif
