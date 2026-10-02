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

// Обои из BMP-файла. 1/0.
static inline int ms_wallpaper(const char* path) {
    return __ms_sc1(SYS_SET_WALLPAPER, (unsigned int)path);
}

#endif
