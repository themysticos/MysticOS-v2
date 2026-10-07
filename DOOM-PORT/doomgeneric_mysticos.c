// doomgeneric_mysticos.c - платформенный слой Doom для MysticOS.
// 6 функций DG_* + main. Вывод - в окно через ms_frame (32bpp кадр).
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

// Скан-код PS/2 -> doom key. ms_key_get() даёт ASCII, поэтому мапим его.
static unsigned char convertToDoomKey(int ch) {
    switch (ch) {
    case 13: case 10: return KEY_ENTER;
    case 27:       return KEY_ESCAPE;
    case 'w': case 'W': return KEY_UPARROW;
    case 's': case 'S': return KEY_DOWNARROW;
    case 'a': case 'A': return KEY_LEFTARROW;
    case 'd': case 'D': return KEY_RIGHTARROW;
    case ' ':      return KEY_FIRE;
    case 'e': case 'E': return KEY_USE;
    case 'y': case 'Y': return 'y';
    default:
        if (ch >= 32 && ch < 127) return (unsigned char)ch;
        return 0;
    }
}

static void addKeyToQueue(int pressed, unsigned char key) {
    unsigned short kd = (unsigned short)((pressed << 8) | key);
    s_KeyQueue[s_KeyQueueWriteIndex] = kd;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

static void handleKeyInput(void) {
    while (ms_key_avail()) {
        int ch = ms_key_get();
        if (ch < 0) break;
        unsigned char dk = convertToDoomKey(ch);
        if (dk) addKeyToQueue(1, dk);       // нажатие
        // Отпускание не отслеживаем (упрощённо) - Doom и так работает.
    }
}

void DG_Init(void) {
    g_win = ms_window("DOOM", 20, 20, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_DrawFrame(void) {
    if (g_win >= 0)
        ms_frame(g_win, (const unsigned int*)DG_ScreenBuffer,
                 DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    handleKeyInput();
    ms_yield();
}

void DG_SleepMs(uint32_t ms) { ms_sleep(ms); }

uint32_t DG_GetTicksMs(void) { return (uint32_t)ms_ticks(); }

int DG_GetKey(int* pressed, unsigned char* doomKey) {
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) return 0;
    unsigned short kd = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
    *pressed = kd >> 8;
    *doomKey = kd & 0xFF;
    return 1;
}

void DG_SetWindowTitle(const char* title) { (void)title; }

// Точка входа ELF-программы.
int _start(void) {
    char* argv[] = { "doom", 0 };
    doomgeneric_Create(1, argv);
    for (;;) doomgeneric_Tick();
    return 0;
}
