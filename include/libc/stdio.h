// libc/stdio.h - ввод-вывод (общая libc MysticOS).
// printf-семейство (в окно/консоль) + FILE (пока memory-backed для WAD).
#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

typedef unsigned int size_t;
#define NULL ((void*)0)
#define EOF (-1)

// ---- printf-семейство ----
int printf(const char* fmt, ...);
int fprintf(void* stream, const char* fmt, ...);
int vfprintf(void* stream, const char* fmt, __builtin_va_list ap);
int snprintf(char* buf, size_t n, const char* fmt, ...);
int vsnprintf(char* buf, size_t n, const char* fmt, __builtin_va_list ap);
int sprintf(char* buf, const char* fmt, ...);
int sscanf(const char* s, const char* fmt, ...);
int vsscanf(const char* s, const char* fmt, __builtin_va_list ap);
int puts(const char* s);
int putchar(int c);

// ---- FILE ----
// Поток: либо memory-backed (для WAD), либо «консоль» (stdout/stderr).
typedef struct _libc_FILE FILE;

FILE* fopen(const char* path, const char* mode);
int   fclose(FILE* f);
size_t fread(void* buf, size_t size, size_t nmemb, FILE* f);
size_t fwrite(const void* buf, size_t size, size_t nmemb, FILE* f);
int   fseek(FILE* f, long offset, int whence);
long  ftell(FILE* f);
int   fflush(FILE* f);
int   remove(const char* path);
int   rename(const char* from, const char* to);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE* stdout;
extern FILE* stderr;

#endif // LIBC_STDIO_H
