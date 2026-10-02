// msyscall.c - stub-библиотека для ELF-программ MysticOS 2 в ring3.
//
// Линкуется С КАЖДОЙ программой (см. examples/build.sh).
// Задача: программа вызывает api->window_create(...), а под капотом
// идёт int 0x80 -> kernel syscall dispatcher -> api_window_create.
//
// Программа получает api НЕ от ядра, а через ms_api():
//     int _start(void) {
//         const mystic_api_t* api = ms_api();
//         ...
//     }
//
// В ring0 int 0x80 тоже работает (для отладки диспетчера без ring3).

#include "../api.h"
#include "../dll/msyscall.h"

// ============================================================
// НИЗКОУРОВНЕВЫЙ ВЫЗОВ int 0x80
// ============================================================
// Регистры: eax=номер, ebx,ecx,edx,esi,edi,ebp=аргументы (до 6).
// Возврат: eax.
// ВНИМАНИЕ: ebp нельзя занять обычным "r"-constraint'ом — его держит GCC
// как frame pointer (даже без -fomit-frame-pointer в некоторых случаях).
// Спасаем его вручную через push/pop вокруг int 0x80.
//
// Обёртка на 5 аргументов (для большинства сисколлов).
static inline int sc5(int n, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
        : "memory"
    );
    return ret;
}

// Обёртка на 6 аргументов (для fill/rect/text — им нужен color).
// ebp занят вручную.
static inline int sc6(int n, int a, int b, int c, int d, int e, int f) {
    int ret;
    __asm__ volatile (
        "push %%ebp\n\t"
        "mov %6, %%ebp\n\t"
        "int $0x80\n\t"
        "pop %%ebp\n\t"
        : "=a"(ret)
        : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e), "r"(f)
        : "memory"
    );
    return ret;
}

// ============================================================
// STUB'Ы (сигнатуры совпадают с mystic_api_t)
// ============================================================

// --- Вывод ---
static void s_put_char(char c)                         { sc5(SYS_PUTCHAR, (int)c, 0, 0, 0, 0); }
static void s_print_string(const char* s)              { sc5(SYS_PRINT_STRING, (int)s, 0, 0, 0, 0); }
static void s_print_int(int v)                         { sc5(SYS_PRINT_INT, v, 0, 0, 0, 0); }
static void s_print_hex(unsigned int v)                { sc5(SYS_PRINT_HEX, (int)v, 0, 0, 0, 0); }

// --- Окна ---
static int s_window_create(const char* t, int x, int y, int w, int h) {
    return sc5(SYS_WINDOW_CREATE, (int)t, x, y, w, h);
}
static int s_window_create_ex(const char* t, int x, int y, int w, int h, int k) {
    // kind — 6-й аргумент, ebp
    return sc6(SYS_WINDOW_CREATE_EX, (int)t, x, y, w, h, k);
}
static void s_window_pixel(int id, int x, int y, unsigned int c) {
    sc5(SYS_WINDOW_PIXEL, id, x, y, (int)c, 0);
}
static void s_window_rect(int id, int x, int y, int w, int h, unsigned int c) {
    sc6(SYS_WINDOW_RECT, id, x, y, w, h, (int)c);
}
static void s_window_fill(int id, int x, int y, int w, int h, unsigned int c) {
    sc6(SYS_WINDOW_FILL, id, x, y, w, h, (int)c);
}
static void s_window_text(int id, int x, int y, const char* s, unsigned int fg, unsigned int bg) {
    // 7 аргументов — bg не влезает. Пока фиксируем bg = WIN_TRANSPARENT.
    // Программам, которым нужен bg, надо вызывать напрямую через int 0x80
    // (TODO: сделать расширенное соглашение с аргументами на стеке).
    (void)bg;
    sc6(SYS_WINDOW_TEXT, id, x, y, (int)s, (int)fg, 0);
}
static void s_window_clear(int id, unsigned int c) {
    sc5(SYS_WINDOW_CLEAR, id, (int)c, 0, 0, 0);
}
static void s_window_close(int id)                     { sc5(SYS_WINDOW_CLOSE, id, 0, 0, 0, 0); }
static void s_window_flush(void)                       { sc5(SYS_WINDOW_FLUSH, 0, 0, 0, 0, 0); }
static int s_window_image(int id, int x, int y, int w, int h, const char* p) {
    (void)p;
    // Не поддержано в v1 stub'ов (7 аргументов). TODO.
    return sc6(SYS_WINDOW_IMAGE, id, x, y, w, h, 0);
}
static void s_window_static_text(int id, int x, int y, const char* s, unsigned int fg) {
    sc5(SYS_WINDOW_STATIC_TEXT, id, x, y, (int)s, (int)fg);
}
static void s_window_static_clear(int id) {
    sc5(SYS_WINDOW_STATIC_CLEAR, id, 0, 0, 0, 0);
}

// --- Время/ввод ---
static void s_yield(void)                              { sc5(SYS_YIELD, 0, 0, 0, 0, 0); }
static unsigned int s_ticks(void)                      { return (unsigned int)sc5(SYS_TICKS, 0, 0, 0, 0, 0); }
static void s_sleep(unsigned int ms)                   { sc5(SYS_SLEEP, (int)ms, 0, 0, 0, 0); }
static int s_key_available(void)                       { return sc5(SYS_KEY_AVAILABLE, 0, 0, 0, 0, 0); }
static int s_key_getchar(void)                         { return sc5(SYS_KEY_GETCHAR, 0, 0, 0, 0, 0); }
static int s_mouse_x(void)                             { return sc5(SYS_MOUSE_X, 0, 0, 0, 0, 0); }
static int s_mouse_y(void)                             { return sc5(SYS_MOUSE_Y, 0, 0, 0, 0, 0); }
static int s_mouse_left(void)                          { return sc5(SYS_MOUSE_LEFT, 0, 0, 0, 0, 0); }

// --- Экран ---
static unsigned int s_screen_w(void)                   { return (unsigned int)sc5(SYS_SCREEN_W, 0, 0, 0, 0, 0); }
static unsigned int s_screen_h(void)                   { return (unsigned int)sc5(SYS_SCREEN_H, 0, 0, 0, 0, 0); }

// --- ФС ---
static int s_fs_size(const char* p)                    { return sc5(SYS_FS_SIZE, (int)p, 0, 0, 0, 0); }
static int s_fs_exists(const char* p)                  { return sc5(SYS_FS_EXISTS, (int)p, 0, 0, 0, 0); }
static int s_fs_read(const char* p, unsigned char* buf, unsigned int max) {
    return sc5(SYS_FS_READ, (int)p, (int)buf, (int)max, 0, 0);
}
static int s_fs_write(const char* p, const char* c) {
    return sc5(SYS_FS_WRITE, (int)p, (int)c, 0, 0, 0);
}
static int s_fs_list(const char* p, char* names, int* isdir, unsigned int* sizes, int max) {
    return sc5(SYS_FS_LIST, (int)p, (int)names, (int)isdir, (int)sizes, max);
}
static int s_format(int drive, int fs, int ver) {
    return sc5(SYS_FORMAT, drive, fs, ver, 0, 0);
}

// --- Система ---
static int s_run_program(const char* name)              { return sc5(SYS_RUN_PROGRAM, (int)name, 0, 0, 0, 0); }
static int s_run_program_arg(const char* name, const char* arg) {
    return sc5(SYS_RUN_PROGRAM_ARG, (int)name, (int)arg, 0, 0, 0);
}
static const char* s_get_arg(void)                      { return (const char*)sc5(SYS_GET_ARG, 0, 0, 0, 0, 0); }
static int s_set_wallpaper(const char* p)               { return sc5(SYS_SET_WALLPAPER, (int)p, 0, 0, 0, 0); }
static void s_exit(int code)                            { sc5(SYS_EXIT, code, 0, 0, 0, 0); for(;;) {} }

// ============================================================
// ЛОКАЛЬНЫЙ api (указатели на stub'ы)
// ============================================================
// Программа получает ЭТОТ указатель, а не ядровый g_api.
// Поля совпадают с mystic_api_t — но функции делают int 0x80.
static mystic_api_t g_local_api;
static int g_api_init = 0;

static void init_api(void) {
    if (g_api_init) return;
    g_api_init = 1;
    g_local_api.version = MYSTIC_API_VERSION;
    // v1
    g_local_api.put_char     = s_put_char;
    g_local_api.print_string = s_print_string;
    g_local_api.print_int    = s_print_int;
    g_local_api.print_hex    = s_print_hex;
    // Окна
    g_local_api.window_create    = s_window_create;
    g_local_api.window_create_ex = s_window_create_ex;
    g_local_api.window_pixel     = s_window_pixel;
    g_local_api.window_rect      = s_window_rect;
    g_local_api.window_fill      = s_window_fill;
    g_local_api.window_text      = s_window_text;
    g_local_api.window_clear     = s_window_clear;
    g_local_api.window_close     = s_window_close;
    g_local_api.window_flush     = s_window_flush;
    // Время/ввод
    g_local_api.yield         = s_yield;
    g_local_api.ticks         = s_ticks;
    g_local_api.sleep         = s_sleep;
    g_local_api.key_available = s_key_available;
    g_local_api.key_getchar   = s_key_getchar;
    g_local_api.mouse_x       = s_mouse_x;
    g_local_api.mouse_y       = s_mouse_y;
    g_local_api.mouse_left    = s_mouse_left;
    // ФС
    g_local_api.fs_list   = s_fs_list;
    g_local_api.fs_read   = s_fs_read;
    g_local_api.fs_size   = s_fs_size;
    g_local_api.fs_exists = s_fs_exists;
    g_local_api.fs_write  = s_fs_write;
    g_local_api.format    = s_format;
    g_local_api.run_program = s_run_program;
    g_local_api.run_program_arg = s_run_program_arg;
    g_local_api.get_arg = s_get_arg;
    g_local_api.set_wallpaper = s_set_wallpaper;
    // Экран
    g_local_api.screen_w = s_screen_w;
    g_local_api.screen_h = s_screen_h;
    // Дополнительные
    g_local_api.window_image       = s_window_image;
    g_local_api.window_static_text  = s_window_static_text;
    g_local_api.window_static_clear = s_window_static_clear;
    g_local_api.exit = s_exit;
}

const mystic_api_t* ms_api(void) {
    init_api();
    return &g_local_api;
}
