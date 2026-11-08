# MysticOS 2

Самописная 32-битная ОС (x86, protected mode, ring3) на C и ассемблере.
Разработка: ~1 год активной работы. 
Сборка: `make run` (WSL + QEMU).

## Что работает

### Ядро
- Загрузчик: MBR → stage2 → kernel.elf (LBA 16), VBE 800x600
- GDT с ring3 + TSS, IDT, PIC + APIC + IO-APIC (WHPX-совместимо)
- Кооперативные задачи (task_create_elf/pe), изоляция через paging
- pmem (4 МБ блоки), heap для программ через SYS_MEM_ALLOC
- mmdcm: V86 + IVT-ловушки (DOS-программы через int 21h)
- Таймеры: PIT 100 Гц + APIC timer

### Драйверы
- **virtio-gpu**: 2D (resource_create_2d, attach_backing, transfer, flush),
  аппаратный курсор (cursorq). В QEMU — software-курсор (vendor 0x1AF4).
- **virtio-net**: RX/TX, ARP, DHCP, DNS, ICMP (ping), UDP, TCP (клиент),
  HTTP-клиент в ядре (api_http_start/poll/result).
- **intel-hda**: звук (PCM 48 кГц, кольцо, микшер из IRQ0).
- **ATA PIO**: FAT16 (чтение/запись/бинарная запись), MSFS (своя ФС).
- **PS/2**: клавиатура (очередь событий), мышь (очередь событий + rel-дельта).
- **PCI**: сканирование, BAR, capabilities.

### Файловые системы
- **FAT16**: чтение, запись, бинарная запись (`fat_write_path_bin`),
  файловый дескриптор (open/read/seek/close). VFS-абстракция (`fs_driver_t`).
- **MSFS** (Mystic FS v2): блочная цепочка, in-band маркеры, free-list.

### Форматы исполняемых файлов
- **ELF32** (entry=_start, Ttext=0x4000000)
- **PE** (MinGW-совместимый, ImageBase=0x4000000)
- **MPF** (свой формат)
- **.msdll** (своя DLL-подсистема: экспорты, релокации)

### API (int 0x80, ebx=указатель на массив аргументов)
- Окна: create/create_ex/pixel/rect/fill/text/clear/close/flush/frame/image
- ФС: list/read/size/exists/write/write_bin/format + file_open/read/seek/close/size
- Ввод: key_available/getchar/event, mouse_x/y/left/grab/rel/event
- Сеть: HTTP (start/poll/result), net_status
- DLL: resolve/load/count
- Система: exit/ticks/sleep/yield/run_program/run_program_arg/get_arg/
  set_wallpaper/screen_w/h/debug/mem_alloc/mem_free

### Программы (examples/, ELF в /kernel/SYSPR/ и корне)
- TERM, FILES, ABOUT, WPICK, IMGVIEW, EDITOR, PAINTER, BROWSER, MUSIC,
  WINAPP, DOOM-PORT (doomgeneric + OPL3 + GENMIDI)

### UI-тулкит (include/ms/ui.h)
- Утилиты: ui_text_w, ui_hover, ui_click, ui_clamp, ui_center_x,
  ui_dim, ui_blend, ui_lpad
- Виджеты: icon_button, button, button_ex, panel, statusbar, list_row,
  empty, separator, header, progress, checkbox, radio, checkbox_row,
  tabs, slider, scrollbar, textfield, tooltip, badge, card
- Event-driven API (v2): ui_begin(win), ui_clicked, ui_hover_client,
  ui_btn, ui_icon_btn, ui_checkbox_ev
- Иконки: BMP из /icons/ui/ + процедурный fallback (21 вид)

### TLS (своя реализация, tls/)
- SHA-256 (NIST-вектор OK)
- HMAC-SHA256 (RFC 4231 OK)
- RDRAND + fallback jitter (rand_bytes)
- AES-128-CBC (NIST SP 800-38A OK)
- BigInt 2048 бит (add/sub/mul/divmod/modpow) — modpow 2^10 mod 1000 = 24,
  3^100 mod 101 = 1 OK
- RSA-2048 (PKCS#1 v1.5 encrypt/verify)
- X.509 DER парсер (извлечение RSA pubkey)
- TLS 1.2 handshake (RSA + AES-128-CBC + SHA-256) — **В РАЗРАБОТКЕ**

## Сборка

```bash
# Полная сборка + запуск (~3 мин, пересоздаёт образ)
make run

# Быстрая итерация: только kernel.bin + dd в образ (~4 сек)
# Используй, если менял только kernel.c / tls/* / net.c / tcp.c
make krun
```
**Важно**: browser.c, DOOM, examples — лежат в FAT-образе, dd их
не обновит. Для них — полный make run.

### Структура
text
kernel.c          ядро (2600 строк): init, API, окна, задачи, syscalls
tls/              TLS (своя реализация): sha256, hmac, rand, aes, bigint, rsa, x509, tls
net.c, tcp.c      сеть: ARP/DHCP/DNS/UDP/TCP/HTTP
virtio*.c         virtio-gpu, virtio-net, virtio (общий)
hda.c, music.c    звук (HDA + кольцо PCM)
vfs.c, fat.c, msfs.c  ФС
exec.c, pe.c      загрузка ELF/PE
dll/msdll.c       DLL-подсистема
include/ms/       API для программ (window, fs, input, system, ui, net, color)
examples/         программы (browser.c и др.)
DOOM-PORT/        порт DOOM (doomgeneric + opl)
## Известные проблемы
TLS: Google шлёт Alert (handshake_failure) — вероятно, требует ECDHE
(RSA key exchange устарел). Нужно добавить ECDHE (X25519/secp256r1)
или проверить на сервере с RSA (info.cern.ch).

Браузер: клики по ссылкам работают, но link_at может путать
многострочные ссылки (len считается по одной строке). Hover есть.
Кнопка «Назад» перезагружает (история не ведётся).

Звук: HDA в QEMU даёт щелчки при максимизации окна (баг QEMU,
не ОС). SB16 — не трогать (вешает QEMU). AC97 — не написан.

DOOM: сейвы — M_WriteFile/M_ReadFile подменены на SYS_FS_*,
но не проверено в бою. Музыка (OPL) — loop с задержкой 50 мс.

AMD GPU драйвер — написан, но не проверен (ОС не грузится на
реальном железе).

Соглашения
Все syscall'ы: eax=номер, ebx=указатель на массив аргументов

Пути: forward slashes, D0:/D1: для диска

.bak на каждый файл (страховка) — при правках создаётся автоматически
