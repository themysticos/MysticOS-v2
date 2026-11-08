// libc/stdio.c - printf-семейство и FILE (общая libc MysticOS).
//
// Вывод printf идёт в консоль/терминал программы (ms_putchar через syscall).
// FILE пока memory-backed (для WAD Doom). fopen читает файл через VFS.
#include "../include/libc/stdio.h"
#include "../include/libc/string.h"
#include "../include/libc/stdlib.h"
#include "../include/ms/syscall.h"

// ============================================================
// Вывод одного символа (в консоль программы).
// SYS_PUTCHAR = 2 (см. dll/msyscall.h).
// ============================================================
static void out_char(char c) {
    __ms_sc1(SYS_PUTCHAR, (unsigned int)(unsigned char)c);
    // Serial-дубль УБРАН: каждый символ через int 0x80 в COM1 чудовищно
    // тормозил (serial в QEMU медленный), а printf зовётся в горячем пути.
}
static void out_str(const char* s) { while (*s) out_char(*s++); }

// ============================================================
// Ядро форматирования: пишет в буфер (для vsnprintf) или в out_char.
// ============================================================
typedef struct {
    char* buf;      // если != NULL - пишем в буфер
    size_t cap;
    size_t pos;
    int    to_console;   // если 1 - пишем через out_char
} sink_t;

static void sink_putc(sink_t* k, char c) {
    if (k->to_console) { out_char(c); return; }
    if (k->pos + 1 < k->cap) k->buf[k->pos] = c;
    k->pos++;
}
static void sink_puts(sink_t* k, const char* s) { while (*s) sink_putc(k, *s++); }

static void fmt_uint(sink_t* k, unsigned long v, int base, int upper, int width, char pad, int prec) {
    char tmp[32]; int n = 0;
    const char* digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = digs[v % base]; v /= base; }
    if (prec > n) while (n < prec) tmp[n++] = '0';   // минимум цифр (%.3d)
    while (n < width) tmp[n++] = pad;
    for (int i = n - 1; i >= 0; i--) sink_putc(k, tmp[i]);
}

static void fmt_int(sink_t* k, long v, int width, char pad, int prec) {
    if (v < 0) { sink_putc(k, '-'); fmt_uint(k, (unsigned long)(-v), 10, 0, width, pad, prec); }
    else fmt_uint(k, (unsigned long)v, 10, 0, width, pad, prec);
}

static void fmt_fixed(sink_t* k, double v, int prec) {
    if (v < 0) { sink_putc(k, '-'); v = -v; }
    unsigned long ip = (unsigned long)v;
    fmt_uint(k, ip, 10, 0, 0, ' ', 0);
    if (prec > 0) {
        sink_putc(k, '.');
        double frac = v - (double)ip;
        for (int i = 0; i < prec; i++) { frac *= 10; }
        unsigned long fp = (unsigned long)(frac + 0.5);
        fmt_uint(k, fp, 10, 0, prec, '0', prec);
    }
}

static void vformat(sink_t* k, const char* fmt, __builtin_va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { sink_putc(k, *fmt); continue; }
        fmt++;
        char pad = ' '; int width = 0; int prec = -1;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == '.') { fmt++; prec = 0; while (*fmt >= '0' && *fmt <= '9') { prec = prec*10 + (*fmt - '0'); fmt++; } }
        switch (*fmt) {
        case 'd': case 'i': fmt_int(k, __builtin_va_arg(ap, int), width, pad, prec); break;
        case 'u': fmt_uint(k, __builtin_va_arg(ap, unsigned int), 10, 0, width, pad, prec); break;
        case 'x': fmt_uint(k, __builtin_va_arg(ap, unsigned int), 16, 0, width, pad, prec); break;
        case 'X': fmt_uint(k, __builtin_va_arg(ap, unsigned int), 16, 1, width, pad, prec); break;
        case 'c': sink_putc(k, (char)__builtin_va_arg(ap, int)); break;
        case 's': { const char* s = __builtin_va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    if (prec >= 0) { for (int i = 0; i < prec && s[i]; i++) sink_putc(k, s[i]); }
                    else sink_puts(k, s);
                    break; }
        case 'f': fmt_fixed(k, __builtin_va_arg(ap, double), 6); break;
        case 'p': sink_putc(k, '0'); sink_putc(k, 'x');
                  fmt_uint(k, (unsigned long)(size_t)__builtin_va_arg(ap, void*), 16, 0, 8, '0', 0); break;
        case '%': sink_putc(k, '%'); break;
        default: sink_putc(k, '%'); if (*fmt) sink_putc(k, *fmt); break;
        }
    }
}

int vsnprintf(char* buf, size_t n, const char* fmt, __builtin_va_list ap) {
    sink_t k = { buf, n, 0, 0 };
    vformat(&k, fmt, ap);
    if (n > 0) { size_t end = k.pos < n ? k.pos : n - 1; buf[end] = 0; }
    return (int)k.pos;
}
int snprintf(char* buf, size_t n, const char* fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap); __builtin_va_end(ap); return r;
}
int sprintf(char* buf, const char* fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap); __builtin_va_end(ap); return r;
}
int vfprintf(void* stream, const char* fmt, __builtin_va_list ap) {
    (void)stream;
    sink_t k = { 0, 0, 0, 1 };
    vformat(&k, fmt, ap);
    return (int)k.pos;
}
int printf(const char* fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = vfprintf(0, fmt, ap); __builtin_va_end(ap); return r;
}
int fprintf(void* stream, const char* fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = vfprintf(stream, fmt, ap); __builtin_va_end(ap); return r;
}
int puts(const char* s) { out_str(s); out_char('\n'); return 0; }
int putchar(int c) { out_char((char)c); return c; }

// ============================================================
// sscanf (минимум: %d %x %s %c %f)
// ============================================================
int vsscanf(const char* s, const char* fmt, __builtin_va_list ap) {
    int count = 0;
    while (*fmt) {
        if (*fmt == ' ') { fmt++; while (*s == ' ') s++; continue; }
        if (*fmt != '%') { if (*s != *fmt) break; fmt++; s++; continue; }
        fmt++;
        while (*s == ' ') s++;
        if (*fmt == 'd') { int* p = __builtin_va_arg(ap, int*); int v = 0, sg = 1;
            if (*s == '-') { sg = -1; s++; }
            while (*s >= '0' && *s <= '9') { v = v*10 + (*s-'0'); s++; } *p = v*sg; count++; }
        else if (*fmt == 'x') { unsigned int* p = __builtin_va_arg(ap, unsigned int*); unsigned int v = 0;
            while (1) { char c = *s; int d;
                if (c>='0'&&c<='9') d=c-'0'; else if (c>='a'&&c<='f') d=c-'a'+10;
                else if (c>='A'&&c<='F') d=c-'A'+10; else break; v=v*16+d; s++; } *p = v; count++; }
        else if (*fmt == 's') { char* p = __builtin_va_arg(ap, char*);
            while (*s && *s != ' ') *p++ = *s++; *p = 0; count++; }
        else if (*fmt == 'c') { char* p = __builtin_va_arg(ap, char*); *p = *s++; count++; }
        else if (*fmt == 'f') { double* p = __builtin_va_arg(ap, double*); *p = atof(s);
            while (*s && *s != ' ') s++; count++; }
        fmt++;
    }
    return count;
}

int sscanf(const char* s, const char* fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

// ============================================================
// FILE: memory-backed (для WAD).
// fopen читает файл ЦЕЛИКОМ через VFS в память.
// ============================================================
struct _libc_FILE {
    unsigned char* data;
    size_t size;
    size_t pos;
    int    is_console;
};

static FILE console_out = { 0, 0, 0, 1 };
static FILE console_err = { 0, 0, 0, 1 };
FILE* stdout = &console_out;
FILE* stderr = &console_err;

FILE* fopen(const char* path, const char* mode) {
    (void)mode;
    // Размер файла через VFS.
    int sz = __ms_sc1(SYS_FS_SIZE, (unsigned int)path);
    if (sz <= 0) return 0;
    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) return 0;
    f->data = (unsigned char*)malloc((size_t)sz);
    if (!f->data) { free(f); return 0; }
    int got = __ms_sc3(SYS_FS_READ, (unsigned int)path, (unsigned int)f->data, (unsigned int)sz);
    { extern void ms_debug(const char*);
      char _b[32]; int _n=0, _v=got; if(!_v)_b[_n++]='0'; while(_v){_b[_n++]='0'+_v%10;_v/=10;}
      ms_debug("[fopen] "); ms_debug(path); ms_debug(" got=");
      while(_n) ms_debug((char[]){_b[--_n],0});
      ms_debug("\n"); }
    if (got < 0) { free(f->data); free(f); return 0; }
    f->size = (size_t)got;
    f->pos = 0;
    f->is_console = 0;
    return f;
}

int fclose(FILE* f) {
    if (!f || f->is_console) return 0;
    if (f->data) free(f->data);
    free(f);
    return 0;
}

size_t fread(void* buf, size_t size, size_t nmemb, FILE* f) {
    if (!f || f->is_console) return 0;
    size_t want = size * nmemb;
    size_t avail = f->size - f->pos;
    if (want > avail) want = avail;
    memcpy(buf, f->data + f->pos, want);
    f->pos += want;
    return want / (size ? size : 1);
}

int fgetc(FILE* f) {
    if (!f || f->is_console) return -1;
    if (f->pos >= f->size) return -1;
    return (int)f->data[f->pos++];
}

// FILE над буфером в памяти (чтение). Буфер НЕ копируется — должен жить,
// пока используется поток. Полезно для парсеров (MIDI/MUS из памяти).
FILE* fmemopen(const void* buf, size_t size) {
    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) return 0;
    f->data = (unsigned char*)buf;
    f->size = size;
    f->pos = 0;
    f->is_console = 0;
    return f;
}

size_t fwrite(const void* buf, size_t size, size_t nmemb, FILE* f) {
    (void)buf; (void)size; (void)nmemb; (void)f;
    return 0;
}

int fseek(FILE* f, long offset, int whence) {
    if (!f || f->is_console) return -1;
    long np = 0;
    if (whence == SEEK_SET) np = offset;
    else if (whence == SEEK_CUR) np = (long)f->pos + offset;
    else if (whence == SEEK_END) np = (long)f->size + offset;
    if (np < 0) np = 0;
    if ((size_t)np > f->size) np = (long)f->size;
    f->pos = (size_t)np;
    return 0;
}

long ftell(FILE* f) { return f ? (long)f->pos : -1; }
int  fflush(FILE* f) { (void)f; return 0; }
int  remove(const char* path) { (void)path; return -1; }
int  rename(const char* a, const char* b) { (void)a; (void)b; return -1; }
