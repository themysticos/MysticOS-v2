// testexe.c - PE-программа, ИМПОРТИРУЮЩАЯ функции из testdll.dll.
// Результаты показывает В ОКНЕ (не в ядровой консоли).
#include "../include/mysticos.h"

__declspec(dllimport) int my_add(int a, int b);
__declspec(dllimport) int my_mul(int a, int b);

static void itoa10(int v, char* out) {
    int p = 0;
    if (v == 0) { out[p++] = '0'; out[p] = 0; return; }
    char t[12]; int n = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    if (neg) t[n++] = '-';
    while (n) out[p++] = t[--n];
    out[p] = 0;
}
static void build(char* out, const char* pre, int num, const char* suf) {
    int p = 0;
    for (int i = 0; pre[i]; i++) out[p++] = pre[i];
    char nb[12]; itoa10(num, nb);
    for (int i = 0; nb[i]; i++) out[p++] = nb[i];
    for (int i = 0; suf[i]; i++) out[p++] = suf[i];
    out[p] = 0;
}

int _start(void) {
    int a = my_add(2, 3);
    int m = my_mul(4, 5);

    int id = ms_window("DLL Test", 150, 120, 320, 160);
    if (id < 0) ms_exit(1);

    ms_clear(id, 0xE8E8F0);
    ms_text_bg(id, 10, 10,  "Imported from testdll.dll:", 0x202040, 0xE8E8F0);

    char buf[64];
    build(buf, "my_add(2,3) = ", a, "");
    ms_text_bg(id, 10, 40, buf, 0x006020, 0xE8E8F0);
    build(buf, "my_mul(4,5) = ", m, "");
    ms_text_bg(id, 10, 60, buf, 0x006020, 0xE8E8F0);

    ms_text_bg(id, 10, 100, "Came via PE import table (IAT).", 0x606080, 0xE8E8F0);
    ms_text_bg(id, 10, 130, "Close window to exit.", 0x808090, 0xE8E8F0);

    for (;;) {
        ms_yield();
        ms_sleep(30);
    }
    ms_exit(0);
    return 0;
}
