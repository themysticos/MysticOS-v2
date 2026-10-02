# Формат .msdll — DLL для MysticOS 2

**Статус:** дизайн-документ. Код загрузчика — отдельный шаг.
**Не путать с PE.** Это свой формат, свой загрузчик, свой ABI.

---

## 0. Зачем DLL, если есть ELF

ELF-программы в M2 — **статические**: весь код внутри файла. Это работает, пока:
- API не меняется (а он меняется — v1 → v2 → v3).
- Программ мало.
- Не хочется грузить код в рантайме (плагины).

DLL решает ровно это:
- Общий код в **одной** копии в памяти.
- API живёт в DLL, программа **импортирует** функции.
- Программы собираются **один раз**, работают с новой DLL без пересборки.
- `LoadLibrary` в рантайме — плагины.

Но DLL **сложнее** ELF: экспорты, импорты, IAT, релокации, общая память.
Этот документ описывает, **как именно** это устроено в MysticOS 2.

---

## 1. Ключевое решение: общая область памяти

**DLL грузится в ОДНУ копию, разделяемую всеми задачами.**

Почему так:
- `msgui.dll` хранит состояние окон (список окон, z-порядок, фокус).
  Если каждая задача имеет свою копию — окна **не будут общими**.
- `mskernel.dll` — это API ядра. Дублировать её в каждой задаче бессмысленно.

### Где размещается

Виртуальный адрес DLL должен быть **одинаков** во всех PD задач.
Выбираем **отдельный PDE** (4 МБ страница PSE), например `PDE[25]` = `0x64000000`:

```
PDE[0]  0x00000000  ядро, identity (все задачи видят одно и то же)
PDE[2]  0x00800000  стек задачи       <- per-task, подменяется в paging_task_pd_alloc
PDE[16] 0x04000000  код задачи        <- per-task, подменяется
PDE[24] 0x06000000  буферы окон       <- identity (общие, но это плохо - см. §10)
PDE[25] 0x64000000  DLL-регион        <- ОБЩИЙ, identity во всех PD
...
```

**Важно:** сейчас `paging_task_pd_alloc` копирует **всю** ядровую PD и подменяет только PDE[2] и PDE[16].
Значит DLL-регион (PDE[25]) будет **автоматически** identity во всех задачах —
достаточно, чтобы он был identity в `g_pd` (что уже так, см. `paging_init`).

**Никаких правок `paging.c` для этого не нужно.** Достаточно:
1. Зарезервировать физический блок 4 МБ под DLL-регион в `pmem` (или отдельным аллокатором).
2. Грузить DLL туда по `phys = PDE[25] * 0x400000 = 0x64000000` (identity).
3. Использовать `0x64000000` как базу загрузки DLL.

---

## 2. Формат файла .msdll

Little-endian, все поля выровнены. Заголовок — 64 байта.

```c
#define MSDLL_MAGIC   0x4C44534Du   // 'M','S','D','L' little-endian
#define MSDLL_VERSION 1

typedef struct {
    unsigned int  magic;          // MSDLL_MAGIC
    unsigned int  version;        // 1
    unsigned int  flags;          // бит 0: has_relocs, бит 1: has_entry
    unsigned int  image_size;     // полный размер образа в памяти
    unsigned int  entry_rva;      // RVA точки входа (DllMain) или 0
    unsigned int  export_count;
    unsigned int  export_rva;     // RVA таблицы экспортов
    unsigned int  import_count;
    unsigned int  import_rva;     // RVA таблицы импортов
    unsigned int  reloc_count;
    unsigned int  reloc_rva;      // RVA таблицы релокаций
    unsigned int  section_count;
    unsigned int  section_rva;    // RVA таблицы секций
    unsigned int  reserved[3];    // до 64 байт
} __attribute__((packed)) msdll_header_t;
```

### Экспорт

```c
typedef struct {
    unsigned int name_rva;   // RVA строки с именем функции (ASCIIZ)
    unsigned int func_rva;   // RVA самой функции (относительно базы DLL)
} __attribute__((packed)) msdll_export_t;
```

### Импорт

```c
typedef struct {
    unsigned int dll_name_rva;   // RVA строки с именем DLL ("mskernel", "msgui")
    unsigned int func_name_rva;  // RVA строки с именем функции
    unsigned int iat_rva;        // RVA слота в IAT программы, куда записать адрес
} __attribute__((packed)) msdll_import_t;
```

**IAT — внутри программы, не DLL.** Программа содержит массив `unsigned int` слотов,
загрузчик патчит их при старте. Это ровно как в PE.

### Релокация

```c
typedef struct {
    unsigned int rva;    // где патчить (RVA от базы DLL)
    unsigned int type;   // 1 = ABS32 (прибавить базу DLL)
                         // 2 = REL32 (не поддерживаем в v1)
} __attribute__((packed)) msdll_reloc_t;
```

В v1 поддерживаем **только ABS32** — это 90% случаев при `-fno-pic` коде, где
встречаются абсолютные адреса (например, `mov eax, [global_var]`).

### Секция

```c
typedef struct {
    unsigned int vaddr;      // RVA от базы DLL
    unsigned int filesz;     // сколько байт в файле
    unsigned int memsz;      // сколько в памяти (>= filesz, остальное — нули)
    unsigned int flags;      // 1=R, 2=W, 4=X
    unsigned int file_off;   // смещение в файле
} __attribute__((packed)) msdll_section_t;
```

---

## 3. Как это собирается (linker script)

Пример `dll/msdll.ld`:

```ld
ENTRY(_start)
SECTIONS
{
    . = 0x64000000;   /* база DLL-региона */

    .msdll_hdr : { KEEP(*(.msdll_hdr)) }
    .text      : { *(.text*) }
    .rodata    : { *(.rodata*) }
    .data      : { *(.data*) }
    .bss       : { *(.bss*) *(COMMON) }

    /* Таблицы экспортов/импортов/релокаций — генерит скрипт (см. §4). */
    .msdll_exports : { *(.msdll_exports) }
    .msdll_imports : { *(.msdll_imports) }
    .msdll_relocs  : { *(.msdll_relocs)  }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
```

Компиляция:

```sh
gcc -m32 -ffreestanding -nostdlib -fno-pic -fno-builtin \
    -fno-stack-protector -O2 -Wall -Wextra \
    -c msgui.c -o msgui.o

gcc -m32 -nostdlib -Wl,-T,msdll.ld -Wl,-e,_start \
    -Wl,--build-id=none -o msgui.msdll msgui.o
```

---

## 4. Как генерируются таблицы экспортов/импортов

**Вариант A (простой, для v1):** вручную в C — макросы.

```c
// msgui.c
#include "msdll.h"

MSDLL_EXPORT(window_create, my_window_create);
MSDLL_EXPORT(window_fill,   my_window_fill);

MSDLL_IMPORT("mskernel", memcpy, memcpy);
MSDLL_IMPORT("mskernel", malloc, malloc);
```

Макросы разворачиваются в секции `.msdll_exports` / `.msdll_imports`:

```c
#define MSDLL_EXPORT(name, fn) \
    static const char _exp_name_##fn[] __attribute__((section(".msdll_expnames"))) = #name; \
    __attribute__((section(".msdll_exports"))) \
    const msdll_export_t _exp_##fn = { \
        (unsigned int)_exp_name_##fn, (unsigned int)&fn \
    };
```

**Проблема с IAT:** в PE программа всегда обращается к функции **через** IAT:
`call [__imp_memcpy]`. В C это делается через `__declspec(dllimport)`, а в gcc —
через `-fno-plt` и правильную линковку. С макросами `MSDLL_IMPORT` так **не получится**.

**Упрощение для v1 (рекомендую):** программа **сама** вызывает `msdll_resolve`
один раз при старте и получает указатели:

```c
// программа
#include "msdll.h"

void* ms_memcpy;
void* ms_malloc;

int _start(void) {
    ms_memcpy = msdll_resolve("mskernel", "memcpy");
    ms_malloc = msdll_resolve("mskernel", "malloc");
    // ...
}
```

Это **не PE**, но **проще** и **работает**. Для v2 можно сделать автоматический IAT
через `--wrap` в ld или свой пост-линкер.

---

## 5. Как грузится .msdll (загрузчик в ядре)

```
msdll_load(path):
  1. Если DLL с таким именем уже загружена — вернуть её базу (refcount++).
  2. Прочитать файл с диска в буфер (vfs_read_file).
  3. Проверить magic == "MSDL", version == 1.
  4. Выделить 4 МБ физблок под DLL-регион (или переиспользовать общий пул).
  5. Скопировать секции по vaddr (identity: phys == virt).
  6. Для каждой релокации: *(u32*)(base + rva) += base.
  7. Для каждого экспорта: добавить в глобальную таблицу символов
     (имя -> base + func_rva).
  8. Для каждого импорта: найти в таблице, записать в IAT программы.
     Если DLL ещё не загружена — рекурсивно загрузить.
  9. Если entry_rva != 0 — вызвать DllMain(base, DLL_PROCESS_ATTACH).
 10. Зарегистрировать DLL в глобальной таблице (имя -> база, refcount).
```

### Глобальная таблица DLL

```c
typedef struct {
    char          name[32];
    unsigned int  base;        // 0x64000000 + смещение
    unsigned int  refcount;
    int           loaded;
} msdll_entry_t;

#define MSDLL_MAX 8
static msdll_entry_t g_dlls[MSDLL_MAX];
```

### Таблица символов (для resolve)

```c
typedef struct {
    char          dll_name[32];
    char          func_name[48];
    unsigned int  addr;
} msdll_symbol_t;

#define MSDLL_MAX_SYMS 256
static msdll_symbol_t g_syms[MSDLL_MAX_SYMS];
```

При `msdll_resolve(dll, func)` — линейный поиск. Для 256 символов это ок.

---

## 6. msdll встроенная в ядро (mskernel)

`mskernel.dll` — **не файл**. Это **статическая таблица** в ядре:

```c
// msdll_builtin.c
static const msdll_builtin_export_t mskernel_exports[] = {
    { "window_create", (unsigned int)&api_window_create },
    { "window_fill",   (unsigned int)&api_window_fill   },
    { "fs_read",       (unsigned int)&api_fs_read       },
    { "malloc",        (unsigned int)&api_malloc        },
    { "free",          (unsigned int)&api_free          },
    // ...
    { 0, 0 }
};
```

Загрузчик при старте **регистрирует** её как виртуальную DLL:

```c
msdll_register_builtin("mskernel", mskernel_exports);
```

Дальше `msdll_resolve("mskernel", "malloc")` работает **так же**, как для
файловой DLL. Программе всё равно — она видит **только имена**.

---

## 7. Порядок внедрения в M2

**Не начинать с DLL.** Порядок такой:

### Шаг 1. Ring3 для ELF-программ

Сейчас `task_trampoline` вызывает `entry(arg)` в **ring0**. Программа может
выполнить `cli`, `out`, `mov cr3` — и уронить ядро.

Что нужно:
1. В `task_create_elf` положить на стек задачи **iret-кадр** для перехода в ring3:
   - `ss = 0x20` (ring3 data), `esp = stack_top`
   - `eflags = 0x202` (IF=1)
   - `cs = 0x18` (ring3 code), `eip = entry`
2. Вместо `call entry` в `task_trampoline` — сделать `iret`.
3. TSS `esp0` обновлять при каждом переключении задачи
   (сейчас в `gdt.c` он **один раз** ставится в `0x90000` — для ring3
   этого **не хватит**, нужен **свой** стек ядра на задачу).
4. `int 0x80` (syscall) — у тебя уже есть, DPL=3. Но нужно, чтобы
   он **работал** из ring3: сейчас `isr128` не делает `iretd` правильно,
   если CPL изменился (нужно `iretd` с полным кадром).

**Оценка:** ~150–200 строк правок. Это **первый большой шаг**.

### Шаг 2. Разделение памяти: стек ядра на задачу

Сейчас `TSS.esp0 = 0x90000` (общий). Для ring3 нужно:
- Свой стек ядра на задачу (например, в PDE[26] = 0x68000000, 4 МБ на задачу).
- При переключении задачи обновлять `TSS.esp0` и `TSS.ss0`.

**Оценка:** ~50 строк.

### Шаг 3. msdll: парсер и resolve (без загрузки)

- `msdll_parse(header, base)` — валидация.
- `msdll_find_export(base, name)` — поиск.
- `msdll_resolve_builtin(dll, func)` — только для встроенных (mskernel).
- Программа вызывает `api->msdll_resolve(...)` — **новое поле в API**.

**Оценка:** ~200 строк. **Не трогает ядро** (отдельный модуль).

### Шаг 4. msdll: загрузка файловых DLL

- `msdll_load(path)` — читает файл, маппит, применяет релокации.
- `msdll_register_builtin` — регистрирует mskernel.
- Разрешение импортов между DLL.

**Оценка:** ~300 строк + правки `vfs.c` (уже есть `read_file`).

### Шаг 5. msgui.msdll — первая реальная DLL

- Вынести часть оконного API из `kernel.c` в `msgui.c`.
- Собрать как `.msdll`.
- Положить в `/kernel/SYS/msgui.msdll`.
- Программа импортирует `window_create` из `msgui`.

**Оценка:** переезд кода + отладка IAT. **Самый сложный шаг.**

---

## 8. Сборка и упаковка

Добавить в `makefile` (после того как все шаги готовы):

```makefile
MSDLL_CFLAGS = -m32 -ffreestanding -nostdlib -fno-pic -fno-builtin \
               -fno-stack-protector -O2 -Wall -Wextra
MSDLL_LDFLAGS = -m32 -nostdlib -Wl,-T,dll/msdll.ld -Wl,-e,_start \
                -Wl,--build-id=none

msgui.msdll: msgui.c api.h dll/msdll.h dll/msdll.ld
	gcc $(MSDLL_CFLAGS) -c msgui.c -o msgui.o
	gcc $(MSDLL_LDFLAGS) -o $@ msgui.o

# ... в правило $(IMAGE):
	python3 tools/fatput.py $@ msgui.msdll MSGUI.MSDLL kernel/SYS/
```

---

## 9. Что НЕ делать

- **Не тащить PE.** Свой формат проще и полностью под контролем.
- **Не эмулировать WinAPI.** Имена функций — твои (`window_create`, а не `CreateWindowExA`).
- **Не делать 64-битную версию сейчас.** 32-бит хватит надолго.
- **Не грузить DLL в кучу ядра.** Отдельный 4 МБ PDE — обязательно.
- **Не забыть про refcount.** Иначе `msdll_load` дважды прочитает файл и создаст две копии.
- **Не пытаться поддерживать REL32 в v1.** Только ABS32, иначе релокации станут адом.

---

## 10. Известные грабли в текущем M2

1. **`paging_task_pd_alloc` копирует всю ядровую PD.** Это значит, что любое
   изменение `g_pd` (например, добавление PDE[25] для DLL) **автоматически**
   попадает в новые задачи. Для уже запущенных задач — **нет**. Придётся либо
   перезапускать задачи, либо копировать PDE вручную.

2. **`TSS.esp0` один на всё ядро.** Для ring3 это **не сработает** — нужен
   per-task `esp0`. См. шаг 2.

3. **Буферы окон (PDE[24]) — identity во всех задачах.** Это значит, что
   **программа может читать/писать буферы других программ**. Для v1 это ок,
   но для безопасности — плохо. В будущем — PDE per-task.

4. **`int 0x80` в `isr128` не делает `iretd`.** Для ring3 нужен `iretd`
   (потому что CPL меняется, стек переключается через TSS). Сейчас это
   работает только в ring0.

5. **`switch_context` сохраняет `esp`, но не `cr3` в кадре.** `cr3`
   грузится из `g_switch_cr3` **до** `mov esp, edx`. Это работает, но
   хрупко: если кто-то забудет вызвать `switch_set_cr3`, задача переключится
   без смены адресного пространства. В ring3 это станет **критично**.

---

## 11. Итоговая архитектура (когда всё готово)

```
/kernel/
  SYS/
    mskernel.msdll   <- виртуальная, встроена в ядро (не файл)
    msgui.msdll      <- окна, виджеты
    msfs.msdll       <- ФС-хелперы (опционально)
  32/                <- задел на будущее (64-битные версии)

/programs/
  editor.exe         <- импортирует msgui, mskernel
  snake.exe          <- импортирует mskernel (только sleep/key)
  files.exe          <- импортирует msgui, mskernel

Память:
  0x00000000..0x00FFFFFF  ядро (identity)
  0x00800000..0x0080FFFF  стек задачи (per-task, PDE[2])
  0x04000000..0x07FFFFFF  код задачи (per-task, PDE[16])
  0x06000000..0x063FFFFF  буферы окон (identity, общие)
  0x64000000..0x67FFFFFF  DLL-регион (identity, общие, PDE[25])
  0x68000000..0x6BFFFFFF  стеки ядра задач (PDE[26], per-task)
```

---

## 12. Что дальше

1. **Прочитай этот документ целиком.** Если что-то не сходится с твоим
   представлением — скажи, поправлю.
2. **Реши, с какого шага начинать.** Я рекомендую **шаг 1 (ring3)**, потому что
   без него DLL — это просто разделяемая память, а не изоляция.
3. **Скажи, писать ли `msdll.h`** (заголовок с макросами `MSDLL_EXPORT`,
   `MSDLL_IMPORT`, структурами из §2). Это **отдельный файл**, ничего не ломает.
4. **Скажи, писать ли `msdll.c`** (парсер + resolve). Тоже отдельный файл.

Я **не трогаю** `kernel.c`, `task.c`, `paging.c`, `entry.asm`, `makefile` —
пока ты не скажешь «да, встраивай».
