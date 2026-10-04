// msyscall.c - stub-библиотека для ELF-программ MysticOS 2 в ring3.
//
// Линкуется С КАЖДОЙ программой (см. examples/build.sh).
// Программа вызывает api->window_create(...), а под капотом идёт
//     int 0x80
// с соглашением Linux-style:
//     eax = номер сисколла
//     ebx = указатель на массив аргументов (unsigned int args[N], N<=6)
// Возврат: eax.
//
// Программа получает api через ms_api():
//     int _start(void) {
//         const mystic_api_t* api = ms_api();
//         ...
//     }

#include "../api.h"
#include "../dll/msyscall.h"

// ============================================================
// СИСКОЛЛ
// ============================================================
static inline int __sc(int n, const unsigned int* args) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "b"(args)
        : "memory"
    );
    return ret;
}

#define SC0(n)               __sc((n), 0)
#define SC1(n,a)             do { unsigned int _a[1]={(unsigned int)(a)}; __sc((n),_a); } while(0)
#define SC2(n,a,b)           do { unsigned int _a[2]={(unsigned int)(a),(unsigned int)(b)}; __sc((n),_a); } while(0)
#define SC3(n,a,b,c)         do { unsigned int _a[3]={(unsigned int)(a),(unsigned int)(b),(unsigned int)(c)}; __sc((n),_a); } while(0)
#define SC4(n,a,b,c,d)       do { unsigned int _a[4]={(unsigned int)(a),(unsigned int)(b),(unsigned int)(c),(unsigned int)(d)}; __sc((n),_a); } while(0)
#define SC5(n,a,b,c,d,e)     do { unsigned int _a[5]={(unsigned int)(a),(unsigned int)(b),(unsigned int)(c),(unsigned int)(d),(unsigned int)(e)}; __sc((n),_a); } while(0)
#define SC6(n,a,b,c,d,e,f)   do { unsigned int _a[6]={(unsigned int)(a),(unsigned int)(b),(unsigned int)(c),(unsigned int)(d),(unsigned int)(e),(unsigned int)(f)}; __sc((n),_a); } while(0)

// С версией, возвращающей int (для read/find/window_create)
static inline int __sc1(int n, unsigned int a) { unsigned int _a[1]={a}; return __sc(n,_a); }
static inline int __sc2(int n, unsigned int a, unsigned int b) { unsigned int _a[2]={a,b}; return __sc(n,_a); }
static inline int __sc3(int n, unsigned int a, unsigned int b, unsigned int c) { unsigned int _a[3]={a,b,c}; return __sc(n,_a); }
static inline int __sc4(int n, unsigned int a, unsigned int b, unsigned int c, unsigned int d) { unsigned int _a[4]={a,b,c,d}; return __sc(n,_a); }
static inline int __sc5(int n, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e) { unsigned int _a[5]={a,b,c,d,e}; return __sc(n,_a); }
static inline int __sc6(int n, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e, unsigned int f) { unsigned int _a[6]={a,b,c,d,e,f}; return __sc(n,_a); }

// ============================================================
// STUB'Ы
// ============================================================

// --- Вывод ---
static void s_put_char(char c)                         { SC1(SYS_PUTCHAR, (int)c); }
static void s_print_string(const char* s)              { SC1(SYS_PRINT_STRING, s); }
static void s_print_int(int v)                         { SC1(SYS_PRINT_INT, v); }
static void s_print_hex(unsigned int v)                { SC1(SYS_PRINT_HEX, v); }

// --- Окна ---
static int s_window_create(const char* t, int x, int y, int w, int h) {
    return __sc5(SYS_WINDOW_CREATE, (unsigned int)t, x, y, w, h);
}
static int s_window_create_ex(const char* t, int x, int y, int w, int h, int k) {
    return __sc6(SYS_WINDOW_CREATE_EX, (unsigned int)t, x, y, w, h, k);
}
static void s_window_pixel(int id, int x, int y, unsigned int c) {
    SC4(SYS_WINDOW_PIXEL, id, x, y, c);
}
static void s_window_rect(int id, int x, int y, int w, int h, unsigned int c) {
    SC6(SYS_WINDOW_RECT, id, x, y, w, h, c);
}
static void s_window_fill(int id, int x, int y, int w, int h, unsigned int c) {
    SC6(SYS_WINDOW_FILL, id, x, y, w, h, c);
}
static void s_window_text(int id, int x, int y, const char* s, unsigned int fg, unsigned int bg) {
    SC6(SYS_WINDOW_TEXT, id, x, y, s, fg, bg);
}
static void s_window_clear(int id, unsigned int c) {
    SC2(SYS_WINDOW_CLEAR, id, c);
}
static void s_window_close(int id)                     { SC1(SYS_WINDOW_CLOSE, id); }
static void s_window_flush(void)                       { SC0(SYS_WINDOW_FLUSH); }
static int s_window_image(int id, int x, int y, int w, int h, const char* p) {
    return __sc6(SYS_WINDOW_IMAGE, id, x, y, w, h, (unsigned int)p);
}
static void s_window_static_text(int id, int x, int y, const char* s, unsigned int fg) {
    SC5(SYS_WINDOW_STATIC_TEXT, id, x, y, s, fg);
}
static void s_window_static_clear(int id) {
    SC1(SYS_WINDOW_STATIC_CLEAR, id);
}

// --- Время/ввод ---
static void s_yield(void)                              { SC0(SYS_YIELD); }
static unsigned int s_ticks(void)                      { return (unsigned int)__sc(0, 0); }
static void s_sleep(unsigned int ms)                   { SC1(SYS_SLEEP, ms); }
static int s_key_available(void)                       { return __sc1(SYS_KEY_AVAILABLE, 0); }
static int s_key_getchar(void)                         { return __sc1(SYS_KEY_GETCHAR, 0); }
static int s_mouse_x(void)                             { return __sc1(SYS_MOUSE_X, 0); }
static int s_mouse_y(void)                             { return __sc1(SYS_MOUSE_Y, 0); }
static int s_mouse_left(void)                          { return __sc1(SYS_MOUSE_LEFT, 0); }

// --- Экран ---
static unsigned int s_screen_w(void)                   { return (unsigned int)__sc1(SYS_SCREEN_W, 0); }
static unsigned int s_screen_h(void)                   { return (unsigned int)__sc1(SYS_SCREEN_H, 0); }

// --- ФС ---
static int s_fs_size(const char* p)                    { return __sc1(SYS_FS_SIZE, (unsigned int)p); }
static int s_fs_exists(const char* p)                  { return __sc1(SYS_FS_EXISTS, (unsigned int)p); }
static int s_fs_read(const char* p, unsigned char* buf, unsigned int max) {
    return __sc3(SYS_FS_READ, (unsigned int)p, (unsigned int)buf, max);
}
static int s_fs_write(const char* p, const char* c) {
    return __sc2(SYS_FS_WRITE, (unsigned int)p, (unsigned int)c);
}
static int s_fs_list(const char* p, char* names, int* isdir, unsigned int* sizes, int max) {
    return __sc5(SYS_FS_LIST, (unsigned int)p, (unsigned int)names, (unsigned int)isdir, (unsigned int)sizes, max);
}
static int s_format(int drive, int fs, int ver) {
    return __sc3(SYS_FORMAT, drive, fs, ver);
}

// --- Система ---
static int s_run_program(const char* name)              { return __sc1(SYS_RUN_PROGRAM, (unsigned int)name); }
static int s_run_program_arg(const char* name, const char* arg) {
    return __sc2(SYS_RUN_PROGRAM_ARG, (unsigned int)name, (unsigned int)arg);
}
static const char* s_get_arg(void)                      { return (const char*)__sc1(SYS_GET_ARG, 0); }
static int s_http_get(const char* url, char* buf, unsigned int max) {
    return __sc3(SYS_HTTP_GET, (unsigned int)url, (unsigned int)buf, max);
}
static int s_set_wallpaper(const char* p)               { return __sc1(SYS_SET_WALLPAPER, (unsigned int)p); }
static void s_exit(int code) {
    // SYS_EXIT только ПОМЕЧАЕТ задачу (want_exit). Ядро завершит её
    // на следующем yield (api_yield_impl -> task_current_wants_exit).
    // Поэтому после SYS_EXIT крутим yield, пока задача не будет убита.
    SC1(SYS_EXIT, code);
    for (;;) { SC0(SYS_YIELD); }
}

// ============================================================
// ЛОКАЛЬНЫЙ api
// ============================================================
static mystic_api_t g_local_api;
static int g_api_init = 0;

static void init_api(void) {
    if (g_api_init) return;
    g_api_init = 1;
    g_local_api.version = MYSTIC_API_VERSION;
    g_local_api.put_char     = s_put_char;
    g_local_api.print_string = s_print_string;
    g_local_api.print_int    = s_print_int;
    g_local_api.print_hex    = s_print_hex;
    g_local_api.window_create    = s_window_create;
    g_local_api.window_create_ex = s_window_create_ex;
    g_local_api.window_pixel     = s_window_pixel;
    g_local_api.window_rect      = s_window_rect;
    g_local_api.window_fill      = s_window_fill;
    g_local_api.window_text      = s_window_text;
    g_local_api.window_clear     = s_window_clear;
    g_local_api.window_close     = s_window_close;
    g_local_api.window_flush     = s_window_flush;
    g_local_api.yield         = s_yield;
    g_local_api.ticks         = s_ticks;
    g_local_api.sleep         = s_sleep;
    g_local_api.key_available = s_key_available;
    g_local_api.key_getchar   = s_key_getchar;
    g_local_api.mouse_x       = s_mouse_x;
    g_local_api.mouse_y       = s_mouse_y;
    g_local_api.mouse_left    = s_mouse_left;
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
    g_local_api.screen_w = s_screen_w;
    g_local_api.screen_h = s_screen_h;
    g_local_api.window_image       = s_window_image;
    g_local_api.window_static_text  = s_window_static_text;
    g_local_api.window_static_clear = s_window_static_clear;
    g_local_api.http_get = s_http_get;
    g_local_api.exit = s_exit;
}

const mystic_api_t* ms_api(void) {
    init_api();
    return &g_local_api;
}
