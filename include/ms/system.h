// ms/system.h - системные функции для программ MysticOS.
#ifndef MS_SYSTEM_H
#define MS_SYSTEM_H

#include "syscall.h"

// Завершить программу. НЕ ВОЗВРАЩАЕТСЯ. Обязательно в конце _start.
static inline void ms_exit(int code) {
    __ms_sc1(SYS_EXIT, (unsigned int)code);
    for (;;) { __ms_sc0(SYS_YIELD); }   // задача умрёт на следующем yield
}

// Пауза (ms).
static inline void ms_sleep(unsigned int ms) {
    __ms_sc1(SYS_SLEEP, ms);
}

// Тики таймера.
static inline unsigned int ms_ticks(void) {
    return (unsigned int)__ms_sc0(SYS_TICKS);
}

// Размеры экрана.
static inline unsigned int ms_screen_w(void) { return (unsigned int)__ms_sc0(SYS_SCREEN_W); }
static inline unsigned int ms_screen_h(void) { return (unsigned int)__ms_sc0(SYS_SCREEN_H); }

// Запустить ELF-программу без/с аргументом. Возвращает id задачи или -1.
static inline int ms_run(const char* name) {
    return __ms_sc1(SYS_RUN_PROGRAM, (unsigned int)name);
}
static inline int ms_run_arg(const char* name, const char* arg) {
    return __ms_sc2(SYS_RUN_PROGRAM_ARG, (unsigned int)name, (unsigned int)arg);
}

// Аргумент, с которым запущена ТЕКУЩАЯ программа (или "").
static inline const char* ms_arg(void) {
    return (const char*)__ms_sc0(SYS_GET_ARG);
}

// Отладка: вывести строку в serial (COM1). Реализация в libc/posix.c.
void ms_debug(const char* s);

// Память: запросить непрерывный регион у ядра (из pmem, блоки 4 МБ).
// Возвращает адрес (identity: phys==virt) или 0. Регион кратен 4 МБ.
static inline unsigned int ms_mem_alloc(unsigned int bytes) {
    return (unsigned int)__ms_sc1(SYS_MEM_ALLOC, bytes);
}
static inline int ms_mem_free(unsigned int addr, unsigned int bytes) {
    return __ms_sc2(SYS_MEM_FREE, addr, bytes);
}

// Обои из BMP-файла. 1/0.
static inline int ms_wallpaper(const char* path) {
    return __ms_sc1(SYS_SET_WALLPAPER, (unsigned int)path);
}

// ============================================================
// DLL (v4)
// ============================================================
// Найти символ в загруженной DLL. dll_name==0 или "*" — искать во всех.
// Возвращает адрес функции или 0. Сама DLL должна быть уже загружена
// ядром (см. ms_dll_load / запуск программы с DLL).
static inline unsigned int ms_dll_resolve(const char* dll_name, const char* func_name) {
    return (unsigned int)__ms_sc2(SYS_DLL_RESOLVE,
                                  (unsigned int)dll_name, (unsigned int)func_name);
}

// Загрузить .msdll с диска (по имени name и пути path). Возвращает базу или 0.
// Уже загруженную вернёт сразу (refcount++).
static inline unsigned int ms_dll_load(const char* name, const char* path) {
    return (unsigned int)__ms_sc2(SYS_DLL_LOAD,
                                  (unsigned int)name, (unsigned int)path);
}

// Число загруженных DLL (для отладки).
static inline int ms_dll_count(void) {
    return __ms_sc0(SYS_DLL_COUNT);
}

#endif
