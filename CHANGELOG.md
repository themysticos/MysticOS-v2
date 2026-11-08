## CHANGELOG.md

# Changelog

## [Unreleased] — 2026-11-01

### Added
- **TLS (своя реализация)**: SHA-256, HMAC-SHA256, RDRAND+jitter,
  AES-128-CBC, BigInt 2048 (modpow), RSA-2048 PKCS#1 v1.5,
  X.509 DER парсер, TLS 1.2 handshake (RSA+AES-128-CBC+SHA-256).
  Все примитивы проходят self-test (NIST/RFC).
- **UI-тулкит**: 12 утилит (hover/click/dim/blend/lpad/center_x/clamp),
  13 новых виджетов (progress/checkbox/radio/tabs/slider/scrollbar/
  textfield/tooltip/badge/card/separator/header/checkbox_row),
  event-driven API (ui_begin/ui_clicked/ui_btn/ui_icon_btn).
- **Очередь событий мыши** (SYS_MOUSE_EVENT=117): move/down/up с
  координатами. `pic.c` + `input.h` + `ui.h`.
- **Браузер**: тёмная тема (ui.h), тулбар с кнопками, скроллбар,
  статусбар, hover-подсветка ссылок, ретраи (3 попытки), таймаут 8 сек.
- **DNS/TCP таймауты по времени** (timer_ticks), а не по итерациям:
  net_dns_resolve (5 сек), net_tcp_connect (3 сек×3), net_tcp_send (2 сек),
  net_test_tcp (5 сек). Это чинит работу на WHPX (быстрый CPU, реальная сеть).
- **SYS_FS_WRITE_BIN** (206): бинарная запись файла (для сейвов DOOM).
  `fat_write_path_bin` + `vfs_write_bin` + `write_bin` в `fs_driver_t`.
- **DOOM**: сейвы через `ms_fs_write_bin/ms_fs_read_all` (M_WriteFile/
  M_ReadFile/M_FileExists в m_misc.c подменены на SYS_FS_*).
- **make krun**: быстрая итерация (kernel.bin + dd в образ, ~4 сек).
- **MAKEFLAGS += -j$(nproc)**: параллельная сборка.

### Fixed
- **TCP SYN retry**: `send_seq` больше не инкрементится в цикле retry
  (все SYNs с одним seq), bump — при получении SYN-ACK. Это чинит
  «no data» на Cloudflare-сайтах.
- **IP checksum**: считается в BE (побайтово), записывается через `be16`.
  Раньше был LE — QEMU дропал пакеты с данными.
- **virtio_net**: RX/TX работают стабильно.
- **`ui_icon_button`**: был баг `if (hover || (left && hover))` = `if (hover)`,
  нажатие не отличалось от hover. Теперь: нажатие→ACCENT, hover→PANEL2.
- **`ui_scrollbar`/`ui_slider`/`ui_progress`**: убран `long long`
  (не линкуется без libgcc — `__divdi3`).
- **`parse_html`**: `MSYS` (Mystic FS) — in-band маркеры.
- **`browser.c`**: координаты кликов через `ms_window_pos` (работает при
  maximize/перемещении). «Loading...» в стилизованной плашке.

### Known Issues
- **TLS**: Google шлёт Alert (handshake_failure). Вероятно, требует ECDHE
  (RSA key exchange устарел для Google). Нужно: добавить ECDHE
  (X25519/secp256r1) или проверить на info.cern.ch (старый RSA).
- **`tls_connect`**: последний баг — `send_handshake(...) != 0` вместо `< 0`
  (send_handshake возвращает число байт, не 0). **Фикс не применён** —
  применить в новом чате:
  `if (send_handshake(...) != 0) return 0;` → `if (send_handshake(...) < 0) return 0;`
  (все 3 места: ClientHello, ClientKeyExchange, Finished).
- **`recv_record`**: alert type=21 не выводит level/desc — добавить отладку
  (патч готов, но не применён).
- **Браузер**: `link_at` может путать многострочные ссылки. История не ведётся.
- **Звук**: HDA щелчки (QEMU), AC97 не написан, SB16 — не трогать.
- **DOOM**: сейвы не проверены в бою. Музыка loop с задержкой 50 мс.

### Changed
- `TCP_RXBUF`: 2048 → 16384 (TLS: сертификат 2-3 КБ).
- `browser.c`: полностью переписан render() (тёмная тема, тулбар, скроллбар).
- `m_misc.c` (DOOM): M_WriteFile/M_ReadFile/M_FileExists через SYS_FS_*.

## [2026-10-31] — предыдущая сессия

### Added
- TCP-клиент (tcp.c): connect/send/recv_nb/recv/close, обработка SYN-ACK/FIN.
- HTTP-клиент в ядре (api_http_start/poll/result).
- Браузер (browser.c): парсинг HTML, ссылки, адресная строка.
- DOOM-порт: doomgeneric + OPL3 (Nuked) + GENMIDI, запуск в окне.
- Сейвы DOOM: SYS_FS_WRITE_BIN, fat_write_path_bin.

### Fixed
- TCP SYN retry (seq не инкрементится).
- IP checksum (BE).
- DNS/TCP таймауты по времени.
- `dg_music_mysticos.c` убран из сборки (SYS_MUSIC_PLAY не существует).
- OPL loop: `OPL_SetCallback(50000, RestartSong)` (было 5000).

## [2026-10-30] — ранняя сессия

### Added
- virtio-gpu 2D + аппаратный курсор.
- virtio-net: ARP, DHCP, DNS, ICMP.
- intel-hda: звук (PCM 48 кГц, кольцо).
- FAT16 запись, MSFS (своя ФС).
- UI-тулкит (базовый): кнопки, панели, иконки, статусбар.
- Очередь событий клавиатуры (SYS_KEY_EVENT).

### Fixed
- virtio-gpu: конвертация 24→32 bpp через 32-битную запись (было побайтово).
- DOOM: `ms_yield()` в конце `DG_DrawFrame` (флип кадра).
- DOOM: FPS-лимит 35 (busy-wait по `ms_ticks`, не `ms_sleep` — он вешает).
- OPL loop: `RestartSong` без `OPL_ClearCallbacks` (было двойное срабатывание).