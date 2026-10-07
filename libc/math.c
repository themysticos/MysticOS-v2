// libc/math.c - математика (общая libc MysticOS).
// Реализовано через x87 (fsqrt) и разложения (sin/cos) где нужно.
#include "../include/libc/math.h"

double fabs(double x) { return x < 0 ? -x : x; }

double sqrt(double x) {
    double r;
    __asm__ volatile ("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

double floor(double x) {
    long i = (long)x;
    if ((double)i > x) i--;
    return (double)i;
}

double ceil(double x) {
    long i = (long)x;
    if ((double)i < x) i++;
    return (double)i;
}

double sin(double x) {
    double r;
    __asm__ volatile ("fsin" : "=t"(r) : "0"(x));
    return r;
}

double cos(double x) {
    double r;
    __asm__ volatile ("fcos" : "=t"(r) : "0"(x));
    return r;
}

double tan(double x) { return sin(x) / cos(x); }

double atan2(double y, double x) {
    double r;
    __asm__ volatile ("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

double pow(double x, double y) {
    // x^y = exp(y * ln(x)). Упрощённо: целые степени.
    if (y == 0) return 1;
    if (x == 0) return 0;
    int neg = 0; long iy = (long)y;
    if ((double)iy != y) return 0;   // только целые
    if (iy < 0) { neg = 1; iy = -iy; }
    double r = 1;
    for (long i = 0; i < iy; i++) r *= x;
    return neg ? 1.0 / r : r;
}
