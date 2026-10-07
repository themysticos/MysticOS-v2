// msyscall.h - номера сисколлов и соглашения.
//
// Общий файл: включают и ядро (kernel.c: syscall_handler), и stub-обёртки
// в программах (examples/msyscall.c).
//
// Вход: eax = номер, ebx/ecx/edx/esi/edi = аргументы (cdecl-подобно).
// Выход: eax = результат.
//
// Вызов из ring3: int $0x80.
#ifndef MSYSCALL_H
#define MSYSCALL_H

// ============================================================
// Базовые (уже есть): putchar, exit, sleep
// ============================================================
#define SYS_EXIT          1   // (code)
#define SYS_PUTCHAR       2   // (char c)
#define SYS_PRINT_STRING  3   // (const char* s) — строка в ring3-памяти
#define SYS_PRINT_INT     4
#define SYS_PRINT_HEX     5
#define SYS_TICKS         6
#define SYS_SLEEP         7   // (ms)
#define SYS_KEY_AVAILABLE 8
#define SYS_KEY_GETCHAR   9
#define SYS_MOUSE_X      10
#define SYS_MOUSE_Y      11
#define SYS_MOUSE_LEFT   12
#define SYS_SCREEN_W     13
#define SYS_SCREEN_H     14
#define SYS_YIELD        15
#define SYS_KEY_EVENT    16   // () -> (pressed<<8)|scancode, или -1 (для игр)

// ============================================================
// Оконный API
// ============================================================
#define SYS_WINDOW_CREATE     100  // (title*, x, y, w, h) -> id
#define SYS_WINDOW_CREATE_EX  101  // (title*, x, y, w, h, kind) -> id
#define SYS_WINDOW_PIXEL      102  // (id, x, y, color)
#define SYS_WINDOW_RECT       103  // (id, x, y, w, h, color)
#define SYS_WINDOW_FILL       104  // (id, x, y, w, h, color)
#define SYS_WINDOW_TEXT       105  // (id, x, y, s*, fg, bg)
#define SYS_WINDOW_CLEAR      106  // (id, color)
#define SYS_WINDOW_CLOSE      107  // (id)
#define SYS_WINDOW_FLUSH      108  // ()
#define SYS_WINDOW_IMAGE      109  // (id, x, y, w, h, path*) -> 1/0
#define SYS_WINDOW_STATIC_TEXT  110 // (id, x, y, s*, fg)
#define SYS_WINDOW_STATIC_CLEAR 111 // (id)
#define SYS_WINDOW_FRAME        112 // (id, src*, w, h) — быстрый blit кадра

// Мышь для игр (v5): захват + относительная дельта (свободный разворот).
#define SYS_MOUSE_GRAB  113  // (on) -> 0   (1 = захватить, курсор ОС стоит)
#define SYS_MOUSE_REL   114  // (dx*, dy*) -> 1/0   (забрать дельту, обнуляет)

// Звук (v5): PCM-вывод (системные звуки, игры).
#define SYS_SOUND_BEEP  120  // (freq_hz, ms) -> 0   простой тон
#define SYS_SOUND_PCM   121  // (buf*, len, rate) -> 0   8-бит беззнаковый моно-PCM

// ============================================================
// Память (v5): программа просит RAM у ядра (из pmem).
// ============================================================
#define SYS_MEM_ALLOC 600  // (bytes) -> адрес или 0 (непрерывный регион)
#define SYS_MEM_FREE  601  // (addr, bytes) -> 1/0

// Отладка: вывод строки в COM1 (serial). Видно в qemu -serial.
#define SYS_DEBUG 700  // (const char* s) -> 0

// ============================================================
// ФС
// ============================================================
#define SYS_FS_LIST   200  // (path*, names*, isdir*, sizes*, max) -> n
#define SYS_FS_READ   201  // (path*, buf*, max) -> n
#define SYS_FS_SIZE   202  // (path*) -> n
#define SYS_FS_EXISTS 203  // (path*) -> 1/0
#define SYS_FS_WRITE  204  // (path*, content*) -> 1/0
#define SYS_FORMAT    205  // (drive, fs, version) -> 1/0

// ============================================================
// Система
// ============================================================
#define SYS_RUN_PROGRAM      300  // (name*) -> id
#define SYS_RUN_PROGRAM_ARG  301  // (name*, arg*) -> id
#define SYS_GET_ARG          302  // () -> const char*
#define SYS_SET_WALLPAPER    303  // (path*) -> 1/0

// ============================================================
// DLL (v4)
// ============================================================
// Резолв символа из загруженной DLL. dll_name==0 или "*" — искать везде.
// Возвращает адрес функции или 0. НЕ загружает DLL — только ищет.
// Чтобы DLL появилась, программу надо запустить с "DLL:имя.msdll" — см. exec.
#define SYS_DLL_RESOLVE      500  // (dll_name*, func_name*) -> addr
#define SYS_DLL_LOAD         501  // (name*, path*) -> base   (пока нет в ring3)
#define SYS_DLL_COUNT        502  // () -> число загруженных DLL

// ============================================================
// Сеть (v3)
// ============================================================
#define SYS_NET_STATUS       400  // (net_status_t* out) -> 0
#define SYS_HTTP_GET         401  // (url*, buf*, max) -> длина тела или -1  (УСТАРЕЛ, блокирующий)
#define SYS_HTTP_START       402  // (url*) -> 0/-1  начать запрос (не ждёт)
#define SYS_HTTP_POLL        403  // () -> 0=в процессе, 1=готово, -1=ошибка
#define SYS_HTTP_RESULT      404  // (buf*, max) -> длина тела или -1 (скопировать результат)

#endif // MSYSCALL_H
