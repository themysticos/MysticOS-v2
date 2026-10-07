# Changelog — MysticOS 2

## [Unreleased]

### Добавлено
- **MSFS v2** (этап 1: ключ + шифр + чтение). См. `msfs2_format.md`.
  - Вся ФС зашифрована своим блочным шифром (XTEA-подобный, 32 раунда).
  - Ключ разбросан по первым 59 секторам как [START][data][END] вперемешку;
    собирается по порядку появления маркеров START.
  - Суперблок на секторе 59, метаданные/данные — зашифрованы.
  - Старая шуточная MSFS v1 (bit-reverse, XOR имён) удалена.
- **PE-DLL: импорты через `.idata`**: `pe_load_dll()` грузит PE-DLL
  (раскладка секций + `.edata` -> `msdll_register_export`). `pe_load_imports()`
  читает таблицу импортов PE-программы, авто-грузит нужные DLL (хук
  `pe_load_dll_by_name` в kernel.c: vfs + pe_load_dll) и **заполняет IAT**
  до старта. Программа не зовёт DLL сама — как в Windows.
  Тесты: `dll/test_dllimport.c` (13 проверок: загрузка dll, resolve,
  заполнение IAT, вызов my_add/my_mul).
- **Импорты PE (.idata)**: `pe_load_imports()` в `pe.c`. При загрузке
  PE-программы ядро читает таблицу импортов, резолвит функции через
  `msdll_resolve` (имя DLL без `.dll`) и **заполняет IAT** до старта
  задачи. Программа ничего не зовёт сама — работает как в Windows.
  Вызывается из `task_create_pe` после раскладки секций.
- **PE-загрузчик (`.exe`)** — мультизагрузчик ELF + PE:
  - `pe.c` / `pe.h` — парсер и загрузчик PE32: DOS/NT/Optional-заголовки,
    секции по RVA, `.reloc` (IMAGE_REL_BASED_HIGHLOW).
  - `task_create_pe()` в `task.c` — задача из PE (свой регион, ring3).
  - `run_program()` в `kernel.c` — формат по магии: `'MZ'` → PE, иначе ELF.
    **ELF-путь (`exec.c`) не тронут** — программы обоих форматов работают.
  - `examples/pehello.c` + сборка через `i686-w64-mingw32-gcc` в makefile.
  - `test_pe.c` — хостовый тест PE-загрузчика (10 тестов).
- **dll/** — подсистема `.msdll` (свой формат, не PE):
  - `msdll_format.md` — дизайн-документ: формат, план внедрения, грабли.
  - `msdll.h` / `msdll.c` — заголовок + парсер/resolve/load-from-buffer.
  - `test_msdll.c` + `build_test.sh` — тестовый стенд (24 теста, все зелёные).
  - `README.md` — навигация.
- **Файловая загрузка `.msdll` (v2)**:
  - `msdll_load_image()` — грузит DLL из ПОЛНОГО файла (заголовок 64 б + образ),
    копирует секции по vaddr, применяет ABS32-релокации, вызывает DllMain.
  - `msdll_load_file()` — читает `.msdll` с диска через `vfs_read_file`.
  - `msdll_region_alloc()` / `msdll_region_set()` — bump-аллокатор DLL-региона
    (0x64000000, 4 МБ, PDE[25], identity во всех задачах).
  - **Новая семантика ABS32-релокаций**: слот хранит абсолютный адрес под
    `MSDLL_BASE` (0x64000000); при загрузке сдвигается на `(base - MSDLL_BASE)`.
  - `MSDLL_EXPORT(name, fn)` — макрос для авторов DLL (таблица в `.msdll_exports`).
- **Инструменты сборки `.msdll`**:
  - `dll/msdll.ld` — linker script (база 0x64000000, секции exports/imports/relocs).
  - `tools/msdll_pack.py` — пост-линкер: добавляет 64-байтный заголовок,
    генерит ABS32-релокации (скан абсолютных адресов + валидация по symtab).
  - `dll/build_dll.sh` — сборка примера DLL одной командой.
  - `dll/example_msgui.c` — первая рабочая `.msdll` (2 экспорта).
  - `dll/test_real_dll.c` — хостовый тест: грузит НАСТОЯЩУЮ .msdll и вызывает.
- **Syscall'ы DLL (v4)**: `SYS_DLL_RESOLVE` (500), `SYS_DLL_LOAD` (501),
  `SYS_DLL_COUNT` (502). Обёртки `ms_dll_resolve/load/count` в `include/ms/system.h`.
- **Расширение `.MSL`** (MyStic Library) для файлов DLL. Влезает в FAT16 8.3.
  `.DLL` намеренно оставлено свободным для будущей совместимости с Windows (PE).
  В образ кладётся как `/kernel/SYS/EXAMPLE.MSL`.
- **msdll в ядре** — `msdll_init()` + `msdll_register_builtin("mskernel", ...)`
  в `kernel_main`. Таблица `g_mskernel_exports[]` — 30 функций ядра
  (`window_*`, `fs_*`, `malloc` и т.п.).
- **`dirty`-флаг** у окон — окно перерисовывается, только если программа
  что-то нарисовала с прошлого кадра. Сбрасывается в `api_yield_impl`.
- **`task_t.want_exit`** + `task_request_exit()` + `task_current_wants_exit()`
  — закрытие окна крестиком помечает на выход **именно ту задачу**, которой
  принадлежит окно. Раньше был `g_exit_request`, убивавший случайную задачу.

### Изменено
- **`window_t.task_id`** — окно знает своего владельца.
- **`api_window_create`** — пишет `task_id = task_current()`.
- **Клик по телу окна** — активирует окно. По заголовку — заодно таскает.
- **Maximize** растягивает **любое** окно (не только системное). Буферы
  окон увеличены до `PWIN_MAXW=800 x PWIN_MAXH=600` (~23 МБ). Обои и
  src перенесены (`WALL_BUF_ADDR=0x07600000`, `WALL_SRC_ADDR=0x07760000`).
- **Stride буфера окна = `PWIN_MAXW`** (не `w->w`) — фикс «смазывания»
  при maximize. Затронуто: `api_window_pixel`, `textbuf_pixel`,
  `api_window_static_clear`, `api_window_image`, `draw_window` blit.
- **Троттлинг перерисовки** в `api_yield_impl` не срабатывает при
  накопленном `dirty_w` — иначе последний кадр после drag теряется.

### Удалено
- `files_*` логика из ядра (`files_render`, `files_pick`, `files_enter`,
  `files_go_up`, `files_reload`, `files_full_path`, `f_names`, `f_isdir`,
  `f_sizes`, `f_count`, `g_files_path`, `g_files_last_*`). Файловый
  менеджер теперь полностью в `FILES.ELF` через API.
- `content_terminal`, `content_files`, `content_about` — окна теперь
  все программные, `draw_content` остался как задел на будущее.
- `open_sys_win` — не нужен (окна создаются через `api_window_create`).
- `in_title` — заменён инлайн-проверкой в `process_events`.
- `make_mypc_fallback`, `mypc_fallback[]`, `mypc_fb_ready` (icons.c).

### Исправлено
- **ring3-программы падали с #PF (CR2=0, EIP=0) при выходе.** `browser.c`
  и `term.c` завершались `return 0` — а в ring3 `return` снимает мусор
  со стека и прыгает на адрес 0 → protection fault (P=1 W=0 U=1).
  Заменено на `ms_exit(0)`. (Старые программы `files/wpick/imgview/editor`
  на старом API `api->` — они в ring3 не запускаются, их не трогали.)
- **Клавиатура: ввод только в активное окно.** Раньше `kb_available()`/
  `kb_getchar()` были глобальными, и при двух окнах-«писателях» (напр. два
  терминала) клавиши распределялись между задачами случайно — печаталось
  сразу в двух. Теперь syscall'ы `SYS_KEY_AVAILABLE`/`SYS_KEY_GETCHAR`
  отдают ввод **только задаче активного (в фокусе) окна**
  (`focused_task_id()`). Кликнул окно — его задача получает клавиатуру;
  остальные `ms_key_avail()` видят 0.
- **КРИТИЧНО: перекрытие ядра с FAT на образе.** `mkfs.fat -R 256`
  резервировал 256 секторов, а ядро (с LBA 16) выросло до ~245 секторов
  (до LBA 260) → `fatput.py` при упаковке файлов писал таблицу FAT
  ПОВЕРХ хвоста ядра (vaddr ~0x2E000, где лежат `gdt_start`/`gdt_desc`).
  GDT грузился мусором → `lgdt` → #GP → triple fault (падение то в
  `gdt_init`, то в `idt_init`, в зависимости от раскладки).
  Фикс: `-R 256` → `-R 512`. Резерв теперь больше ядра.
- **Утечка `text_buf_idx`** при закрытии окна (освобождался только
  `buf_idx`). После ~15 открытий/закрытий окна переставали создаваться.
- **«Битая полоса» справа** от окна после drag — throttle пропускал
  последний кадр при накопленном `dirty`.

### Не сделано (план)
- **Авто-IAT** — сейчас программа сама зовёт `ms_dll_resolve` при старте.
  Автоматический патч IAT при загрузке — задача v2 (см. msdll_format.md §4).
- **REL32-релокации** — только ABS32.
- **`window_size` в API** — программы не знают о maximize.
- **`msgui.msdll`** — UI-фреймворк с виджетами (первый шаг после DLL-load).
- **`window_mark_dirty` в API** — точная перерисовка (только область).
- **Приоритет**: перевести программы ring3 на `ms_dll_resolve` (сейчас
  программы используют syscall'ы напрямую; DLL-путь — новый, опциональный).
