# Changelog — MysticOS 2

## [Unreleased]

### Добавлено
- **dll/** — подсистема `.msdll` (свой формат, не PE):
  - `msdll_format.md` — дизайн-документ: формат, план внедрения, грабли.
  - `msdll.h` / `msdll.c` — заголовок + парсер/resolve/load-from-buffer.
  - `test_msdll.c` + `build_test.sh` — тестовый стенд (24 теста, все зелёные).
  - `README.md` — навигация.
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
- **Утечка `text_buf_idx`** при закрытии окна (освобождался только
  `buf_idx`). После ~15 открытий/закрытий окна переставали создаваться.
- **«Битая полоса» справа** от окна после drag — throttle пропускал
  последний кадр при накопленном `dirty`.

### Не сделано (план)
- **Ring3** — программы работают в ring0. GDT/TSS готовы, нужен `iret`.
- **Файловая загрузка `.msdll`** — сейчас только встроенная `mskernel`.
- **`window_size` в API** — программы не знают о maximize.
- **`msgui.msdll`** — UI-фреймворк с виджетами (после ring3 + DLL-load).
- **`window_mark_dirty` в API** — точная перерисовка (только область).
