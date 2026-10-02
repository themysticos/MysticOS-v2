// mysticos.h - единый include для программ MysticOS.
//
// Использование:
//   #include "../include/mysticos.h"
//
//   int _start(void) {
//       int w = ms_window("Hello", 100, 100, 300, 200);
//       ms_clear(w, MS_WIN_BG);
//       ms_text(w, 10, 10, "Hello, world!", MS_WHITE);
//       ms_yield();
//       while (!ms_key_avail()) { ms_yield(); ms_sleep(30); }
//       ms_exit(0);
//   }
//
// Никаких api->... — только прямые функции.
#ifndef MYSTICOS_H
#define MYSTICOS_H

#include "ms/syscall.h"
#include "ms/color.h"
#include "ms/window.h"
#include "ms/fs.h"
#include "ms/input.h"
#include "ms/system.h"

#endif
