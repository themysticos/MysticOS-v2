// libc/builtins.c - компиляторные builtins и errno (общая libc MysticOS).
#include "../include/libc/stdio.h"

// ---- __errno_location (errno) ----
static int g_errno = 0;
int* __errno_location(void) { return &g_errno; }

// ---- __divdi3: 64-битное ЗНАКОВОЕ деление (long long / long long). ----
// gcc -m32 генерит вызов этого при делении 64-бит.
long long __divdi3(long long a, long long b) {
    int neg = 0;
    unsigned long long ua, ub;
    if (a < 0) { ua = (unsigned long long)(-a); neg ^= 1; } else ua = (unsigned long long)a;
    if (b < 0) { ub = (unsigned long long)(-b); neg ^= 1; } else ub = (unsigned long long)b;
    if (ub == 0) return 0;   // деление на ноль - не должно случаться
    // Длинное деление бит за битом.
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((ua >> i) & 1u);
        if (r >= ub) { r -= ub; q |= (1ull << i); }
    }
    return neg ? -(long long)q : (long long)q;
}

// ---- __udivdi3: 64-битное БЕЗЗНАКОВОЕ деление (unsigned long long). ----
unsigned long long __udivdi3(unsigned long long a, unsigned long long b) {
    if (b == 0) return 0;
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) { r -= b; q |= (1ull << i); }
    }
    return q;
}

// ---- __umoddi3: 64-битный БЕЗЗНАКОВЫЙ остаток. ----
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
    if (b == 0) return 0;
    unsigned long long r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) r -= b;
    }
    return r;
}

// ---- __isoc23_sscanf: glibc-алиас sscanf (новые версии) ----
extern int vsscanf(const char* s, const char* fmt, __builtin_va_list ap);
int __isoc23_sscanf(const char* s, const char* fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
