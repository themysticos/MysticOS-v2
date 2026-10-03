# dll/ — DLL-подсистема MysticOS 2 (в разработке)

Это **не часть ядра**. Отдельный модуль, который встраивается в `kernel.elf`
только когда сам решишь (см. §7 в `msdll_format.md`).

## Что здесь лежит

| Файл | Что | Трогает ядро? |
|------|-----|---------------|
| `msdll_format.md` | дизайн-документ: формат `.msdll`, план внедрения, грабли | нет |
| `msdll.h` | заголовок: структуры формата + API загрузчика | нет |
| `msdll.c` | парсер, resolve, загрузка из буфера, релокации ABS32 | нет |
| `test_msdll.c` | тестовый стенд на 24 проверки (запускается на ХОСТЕ) | нет |
| `build_test.sh` | сборка/запуск теста одной командой | нет |

## Что НЕ здесь (пока)

- **Чтение `.msdll` с диска** — вызывающий сам делает `vfs_read_file`.
- **Автоматический IAT-патч** — см. `msdll_format.md` §4: в v1 программа сама
  зовёт `msdll_resolve` при старте. Авто-IAT — задача v2.
- **REL32-релокации** — только ABS32.
- **Ring3** — без него DLL не имеют изоляции. Отдельный шаг.

## Быстрый старт

```sh
cd dll
./build_test.sh          # собрать и прогнать 24 теста на хосте
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

1. Встроить `msdll.o` в `kernel.elf` (правки `makefile` + `kernel.c`).
2. `msdll_init()` + `msdll_register_builtin("mskernel", ...)` в `kernel_main`.
3. Ring3 — без него DLL это просто разделяемая память.
4. Файловые DLL (`msdll_load` с чтением диска).
5. `msgui.msdll` — первая реальная DLL.

Подробности — в `msdll_format.md`, §7.
