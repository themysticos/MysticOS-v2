# dll/ — DLL-подсистема MysticOS 2 (в разработке)

Это **не часть ядра**. Отдельный модуль, который встраивается в `kernel.elf`
только когда сам решишь (см. §7 в `msdll_format.md`).

## Что здесь лежит

| Файл | Что | Трогает ядро? |
|------|-----|---------------|
| `msdll_format.md` | дизайн-документ: формат `.msdll`, план внедрения, грабли | нет |
| `msdll.h` | заголовок: структуры формата + API загрузчика + `MSDLL_EXPORT` | нет |
| `msdll.c` | парсер, resolve, загрузка из буфера/файла, релокации ABS32 | нет |
| `msdll.ld` | linker script для `.msdll` (база 0x64000000) | нет |
| `example_msgui.c` | пример DLL (2 экспорта) | нет |
| `build_dll.sh` | сборка примера `.msdll` одной командой | нет |
| `test_msdll.c` | unit-стенд на 27 проверок (запускается на ХОСТЕ) | нет |
| `test_real_dll.c` | тест загрузки НАСТОЯЩЕЙ `.msdll` (12 проверок, на ХОСТЕ) | нет |
| `build_test.sh` | сборка/запуск всех тестов одной командой | нет |
| `msyscall.h` | номера syscall'ов (вкл. SYS_DLL_*) | нет |

## Что НЕ здесь (пока)

- **Автоматический IAT-патч** — см. `msdll_format.md` §4: в v1 программа сама
  зовёт `ms_dll_resolve` при старте. Авто-IAT — задача v2.
- **REL32-релокации** — только ABS32.

## Готово

- **Ring3** — программы работают в ring3 (GDT/TSS/iret).
- **Загрузка `.msdll` с диска** — `msdll_load_file()` через `vfs_read_file`.
- **DLL-регион** — 0x64000000 (PDE[25], identity во всех задачах).
- **Syscall'ы** `SYS_DLL_RESOLVE/LOAD/COUNT` для ring3-программ.

## Быстрый старт

```sh
cd dll
./build_test.sh          # unit (27) + real .msdll (12) тесты на хосте
./build_dll.sh example_msgui.c example_msgui.msdll   # только собрать DLL
./build_test.sh clean    # убрать артефакты
```

Требуется `gcc-multilib` (для `-m32`). В WSL:
```sh
sudo apt install gcc-multilib
```

## Как это выглядит в коде программы (v1, план)

```c
#include "../dll/msdll.h"

// Программа при старте резолвит нужные символы один раз.
static int (*ms_window_create)(const char*, int, int, int, int);
static int (*ms_malloc)(unsigned int);

int _start(const mystic_api_t* api) {
    ms_window_create = (int (*)(const char*, int, int, int, int))
        msdll_resolve("msgui", "window_create");
    ms_malloc = (int (*)(unsigned int))
        msdll_resolve("mskernel", "malloc");
    // ...
}
```

## Что дальше

1. **`msgui.msdll`** — вынести оконный API в отдельную DLL (виджеты, кнопки).
2. **Авто-IAT** — патчить таблицу импортов при загрузке (v2 формата).
3. **REL32-релокации** — для кода с относительными переходами.
4. **`ms_dll_resolve` в программах** — программы сейчас ходят через syscall'ы
   напрямую; DLL-путь новый и опциональный.

## Как автору написать свою DLL

```c
#include "msdll.h"

int my_add(int a, int b) { return a + b; }

int _start(unsigned int base, unsigned int reason) {  // DllMain
    (void)base; (void)reason;
    return 0;
}

MSDLL_EXPORT(add, my_add);   // экспорт под именем "add"
```

Сборка:
```sh
./build_dll.sh my_dll.c my_dll.msdll
```

Загрузка из программы (ring3):
```c
#include "../include/mysticos.h"

int _start(void) {
    ms_dll_load("mydll", "/kernel/SYS/MY_DLL.MSL");
    int (*add)(int,int) = (int (*)(int,int))ms_dll_resolve("mydll", "add");
    int r = add(2, 3);
    ms_exit(r);
}
```

**Расширение:** `.MSL` (MyStic Library) — влезает в FAT16 8.3.
`.DLL` намеренно оставлено свободным для будущей совместимости с Windows (PE).

Подробности — в `msdll_format.md`, §7.
