// libc/stdlib.h - общие функции (malloc, atoi, exit...) для MysticOS.
#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

typedef unsigned int size_t;

#define NULL ((void*)0)

void* malloc(size_t n);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* p, size_t n);
void  free(void* p);

int   atoi(const char* s);
double atof(const char* s);
int   abs(int v);

void  exit(int code) __attribute__((noreturn));
int   system(const char* cmd);

// Установка арены для аллокатора (буфер программы).
// Вызывается в _start до всего остального.
void  libc_init_heap(void* base, size_t size);

#endif // LIBC_STDLIB_H
