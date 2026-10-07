// doomgeneric_mysticos.c - платформенный слой Doom для MysticOS.
// 6 функций DG_* + main. Вывод - в окно через ms_frame (32bpp кадр).
#include <stdio.h>
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

void DG_Init(void) {
    ms_debug("[doom] DG_Init: creating window...\n");
    g_win = ms_window("DOOM", 20, 20, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    ms_debug("[doom] DG_Init: window created\n");
}

extern int D_GrabMouseCallback(void);   // Doom: 1 = сейчас идёт игра (нужен grab)
void DG_DrawFrame(void) {
    static int n = 0;
    if (n++ == 1) ms_debug("[doom] first DG_DrawFrame!\n");
    // Захват мыши ТОЛЬКО во время игры (в меню курсор свободен).
    ms_mouse_grab(D_GrabMouseCallback() ? 1 : 0);
    if (g_win >= 0)
        ms_frame(g_win, (const unsigned int*)DG_ScreenBuffer,
                 DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    handleKeyInput();
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
int _start(void) {
    ms_debug("[doom] _start entered\n");
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
