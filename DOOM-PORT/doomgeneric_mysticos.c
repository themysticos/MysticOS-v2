// doomgeneric_mysticos.c - платформенный слой Doom для MysticOS.
// 6 функций DG_* + main. Вывод - в окно через ms_frame (32bpp кадр).
#include <stdio.h>
#include <stdlib.h>
#include "doomgeneric/doomgeneric/doomgeneric.h"
#include "doomgeneric/doomgeneric/doomkeys.h"
#include "../include/ms/color.h"
#include "../include/ms/window.h"
#include "../include/ms/system.h"
#include "../include/ms/input.h"

static int g_win = -1;

#define KEYQUEUE_SIZE 16
static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

extern int ms_key_event(void);   // (pressed<<8)|scancode, -1 если пусто

// Скан-код PS/2 -> doom key. Работает и для нажатий, и для отпусканий.
static unsigned char convertToDoomKey(int sc) {
    switch (sc & 0x7F) {
    case 0x1C: return KEY_ENTER;
    case 0x01: return KEY_ESCAPE;
    case 0x48: return KEY_UPARROW;
    case 0x50: return KEY_DOWNARROW;
    case 0x4B: return KEY_LEFTARROW;
    case 0x4D: return KEY_RIGHTARROW;
    case 0x39: return KEY_FIRE;     // Space - огонь/открыть
    case 0x1D: return KEY_FIRE;     // Ctrl - огонь
    case 0x11: return KEY_UPARROW;  // W - вперёд
    case 0x1F: return KEY_DOWNARROW;// S - назад
    case 0x1E: return KEY_LEFTARROW;// A - strafe left
    case 0x20: return KEY_RIGHTARROW;// D - strafe right
    case 0x12: return KEY_USE;      // E - использовать (открывать двери)
    case 0x15: return 'y';          // Y (меню)
    case 0x31: return 'n';          // N (меню)
    case 0x2C: return 'z';
    case 0x13: return 'r';
    case 0x24: return 'j';
    default: return 0;
    }
}

static void addKeyToQueue(int pressed, unsigned char key) {
    unsigned short kd = (unsigned short)((pressed << 8) | key);
    s_KeyQueue[s_KeyQueueWriteIndex] = kd;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

static void handleKeyInput(void) {
    int ev;
    while ((ev = ms_key_event()) >= 0) {
        int pressed = (ev >> 8) & 1;
        unsigned char sc = (unsigned char)(ev & 0xFF);
        unsigned char dk = convertToDoomKey(sc);
        if (dk) addKeyToQueue(pressed, dk);   // и нажатие, и ОТПУСКАНИЕ
    }
}

// Запрошенный размер клиентской области (по умолчанию 640x400).
static int g_want_w = 640, g_want_h = 400;

// Low-detail режим (detailLevel=1): R_ExecuteSetViewSize делает
// viewwidth >>= 1 — вдвое меньше столбцов в 3D-растеризации. Главный
// рычаг FPS на медленном CPU (QEMU TCG).
void DG_Init(void) {
    ms_debug("[doom] DG_Init: creating window...\n");
    // Окно: клиент = g_want_w x g_want_h, плюс 20px на заголовок.
    // Так кадр Doom (DOOMGENERIC_RESX/RESY) совпадёт с клиентской областью,
    // и ядро скопирует кадр 1:1 (быстрый путь, без масштабирования).
    g_win = ms_window("DOOM", 20, 20, g_want_w, g_want_h + 20);
    ms_debug("[doom] DG_Init: window created\n");
    // Кадр рендерим в НАТИВНОМ размере (320x200). Масштабирование до
    // клиентской области делает ЯДРО (быстрый fixed-point, frame_ticks=0).
    DOOMGENERIC_RESX = 320;
    DOOMGENERIC_RESY = 200;
    ms_debug("[doom] DG_Init: frame = 320x200 (kernel scales)\n");
}

extern int D_GrabMouseCallback(void);   // Doom: 1 = сейчас идёт игра (нужен grab)

// Переаллокация буфера кадра под новое разрешение (при resize/maximize окна).
// Возвращает 1, если размер изменился.
static int dg_resize_frame(int cw, int ch) {
    if (cw == DOOMGENERIC_RESX && ch == DOOMGENERIC_RESY) return 0;
    if (cw <= 0 || ch <= 0) return 0;
    void* nb = malloc((unsigned int)cw * (unsigned int)ch * 4u);
    if (!nb) return 0;
    if (DG_ScreenBuffer) free(DG_ScreenBuffer);
    DG_ScreenBuffer = (pixel_t*)nb;
    DOOMGENERIC_RESX = cw;
    DOOMGENERIC_RESY = ch;
    return 1;
}

// Профилировка фаз кадра Doom (для поиска узкого места).
unsigned int g_dg_t_scale = 0, g_dg_t_blit = 0, g_dg_t_frame = 0;

void DG_ProfMark(int stage, unsigned int t) {
    if (stage == 0) g_dg_t_scale = t;   // I_FinishUpdate: растяжение
    else if (stage == 1) g_dg_t_blit = t; // ms_frame
}

void DG_DrawFrame(void) {
    static int n = 0;
    if (n++ == 1) ms_debug("[doom] first DG_DrawFrame!\n");
    // Захват мыши ТОЛЬКО во время игры (в меню курсор свободен).
    ms_mouse_grab(D_GrabMouseCallback() ? 1 : 0);
    if (g_win >= 0) {
        // Кадр фиксирован 320x200; масштаб под окно делает ядро (1 вызов).
        ms_frame(g_win, (const unsigned int*)DG_ScreenBuffer,
                 DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    }
    handleKeyInput();
    // Насос музыки: каждый кадр доливаем PCM в кольцо ядра, чтобы
    // музыка не обрывалась между тиками игры.
    extern void ms_music_pump(void);
    ms_music_pump();
    // Профилировка FPS: раз в ~2 сек печатаем кадры/сек и время фаз.
    {
        static unsigned int last = 0, frames = 0;
        frames++;
        unsigned int now = ms_ticks();
        if (now - last >= 200) {
            if (last != 0) {
                char b[8]; int bi = 0;
                unsigned int f = frames;
                char tmp[8]; int ti = 0;
                if (f == 0) tmp[ti++] = '0';
                while (f) { tmp[ti++] = '0' + f % 10; f /= 10; }
                while (ti) b[bi++] = tmp[--ti];
                b[bi] = 0;
                ms_debug("[dg] fps=" );
                ms_debug(b);
                ms_debug("\n");
            }
            last = now; frames = 0;
        }
    }
    // Ограничение FPS: DOOM рассчитан на TICRATE=35 Гц. Без ограничения
    // цикл крутится 85-90 FPS, ms_music_pump() вызывается слишком часто и
    // рвано, кольцо PCM то пустеет, то переполняется -> звук двоится.
    // ВАЖНО: ms_sleep() под WHPX вешает задачу (SYS_SLEEP не будит обратно —
    // таймеры задач не тикают). Поэтому busy-wait по ms_ticks(), как в
    // ms_music_pump (он работает и не вешает). ms_yield() внутри НЕ зовём —
    // иначе флип каждый тик; просто крутимся до следующего кадра.
    {
        #define DG_TARGET_FPS 35
        #define DG_FRAME_MS   (1000 / DG_TARGET_FPS)   // 28 мс
        static unsigned int last_frame_ms = 0;
        if (last_frame_ms != 0) {
            while ((unsigned int)ms_ticks() * 10u - last_frame_ms < DG_FRAME_MS) {
                // busy-wait: ms_ticks() тикает, ждём.
            }
        }
        last_frame_ms = (unsigned int)ms_ticks() * 10u;
    }
    // ms_yield() — это НЕ просто отдать CPU: он обслуживает события и
    // ФЛИПАЕТ кадр окна на экран (window_flush). Без него кадр рисуется
    // в буфер, но не показывается. Sleep выше ограничивает FPS, yield
    // ниже — показывает кадр.
    ms_yield();
}

void DG_SleepMs(uint32_t ms) { ms_sleep(ms); }

uint32_t DG_GetTicksMs(void) { return (uint32_t)ms_ticks() * 10u; }  // PIT 100Гц -> мс

int DG_GetKey(int* pressed, unsigned char* doomKey) {
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) return 0;
    unsigned short kd = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
    *pressed = kd >> 8;
    *doomKey = kd & 0xFF;
    return 1;
}

void DG_SetWindowTitle(const char* title) { (void)title; }

// Мышь: Doom ждёт ОТНОСИТЕЛЬНОЕ движение (дельту). Считаем из ms_mouse_x/y.
// Возвращает 1 если движение/клик было, 0 иначе. dx/dy - дельта, btns - кнопки.
int DG_GetMouse(int* dx, int* dy, int* btns) {
    int ddx = 0, ddy = 0;
    ms_mouse_rel(&ddx, &ddy);          // накопленная дельта (без границ экрана)
    int mb = ms_mouse_left();
    static int last_mb = 0;
    int changed = (ddx != 0 || ddy != 0 || mb != last_mb);
    last_mb = mb;
    *dx = ddx; *dy = 0;                // мышь только ПОВОРАЧИВАЕТ (ходьба - клавиши)
    *btns = mb ? 1 : 0;
    return changed;
}

// Точка входа ELF-программы.
// Heap: запрашиваем у ядра 32 МБ (8 блоков pmem) под WAD (11 МБ) + кучу Doom.
extern void libc_init_heap(void* base, unsigned int size);
extern unsigned int ms_mem_alloc(unsigned int bytes);
// Low-detail режим: R_ExecuteSetViewSize делает viewwidth >>= 1 —
// вдвое меньше столбцов 3D-растеризации. Главный рычаг FPS в QEMU TCG.
extern int detailLevel;

// Обёртки для сейвов DOOM (m_misc.c): прямой SYS_FS_* без FILE*.
// Чтение/запись БИНАРНЫЕ (внутри нули), длина явная.
extern int ms_fs_write_bin(const char* path, const unsigned char* buf, unsigned int len) {
    return __ms_sc3(SYS_FS_WRITE_BIN, (unsigned int)path, (unsigned int)buf, len);
}
extern int ms_fs_read_all(const char* path, unsigned char* buf, unsigned int max) {
    return __ms_sc3(SYS_FS_READ, (unsigned int)path, (unsigned int)buf, max);
}
extern int ms_fs_size(const char* path) {
    return __ms_sc1(SYS_FS_SIZE, (unsigned int)path);
}
extern int ms_fs_exists(const char* path) {
    return __ms_sc1(SYS_FS_EXISTS, (unsigned int)path);
}

int _start(void) {
    ms_debug("[doom] _start entered\n");
    detailLevel = 1;    // low-detail ДО R_Init/R_SetViewSize
    libc_init_heap((void*)0x04400000u, 28u * 1024u * 1024u);
    ms_debug("[doom] heap init ok\n");
    char* argv[] = { "doom", "-mb", "8", 0 };   // zone 8 МБ (heap 28 МБ: WAD 11 + zone 8)
    ms_debug("[doom] before create call\n");
    doomgeneric_Create(1, argv);
    ms_debug("[doom] after create call\n");
    ms_debug("[doom] create RETURNED (unexpected!)\n");
    for (;;) doomgeneric_Tick();
    return 0;
}
