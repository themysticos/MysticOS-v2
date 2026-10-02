// ms/syscall.h - низкоуровневый int 0x80 для программ MysticOS.
//
// Программы НЕ вызывают это напрямую. Используй ms/window.h, ms/fs.h и т.п.
#ifndef MS_SYSCALL_H
#define MS_SYSCALL_H

#include "../../dll/msyscall.h"   // SYS_* номера

typedef unsigned int ms_u32;

// Единая точка входа в ядро: eax=номер, ebx=указатель на массив аргументов.
static inline int __ms_sc(int n, const unsigned int* args) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(args)
        : "memory"
    );
    return ret;
}

static inline int __ms_sc0(int n) {
    return __ms_sc(n, 0);
}
static inline int __ms_sc1(int n, unsigned int a) {
    unsigned int v[1] = { a }; return __ms_sc(n, v);
}
static inline int __ms_sc2(int n, unsigned int a, unsigned int b) {
    unsigned int v[2] = { a, b }; return __ms_sc(n, v);
}
static inline int __ms_sc3(int n, unsigned int a, unsigned int b, unsigned int c) {
    unsigned int v[3] = { a, b, c }; return __ms_sc(n, v);
}
static inline int __ms_sc4(int n, unsigned int a, unsigned int b, unsigned int c, unsigned int d) {
    unsigned int v[4] = { a, b, c, d }; return __ms_sc(n, v);
}
static inline int __ms_sc5(int n, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e) {
    unsigned int v[5] = { a, b, c, d, e }; return __ms_sc(n, v);
}
static inline int __ms_sc6(int n, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e, unsigned int f) {
    unsigned int v[6] = { a, b, c, d, e, f }; return __ms_sc(n, v);
}

#endif
