// msdll.c - загрузчик .msdll для MysticOS 2.
//
// Реализует:
//   * парсер заголовка и секций (без чтения диска);
//   * таблицу загруженных DLL;
//   * таблицу символов (имя -> адрес);
//   * resolve(dll, func);
//   * загрузку из уже прочитанного буфера (msdll_load_from_buffer);
//   * применение релокаций ABS32;
//   * регистрацию встроенных DLL (mskernel) — они не файлы.
//
// ЧЕГО НЕ ДЕЛАЕТ (намеренно, для v1):
//   * не читает диск (вызывающий сам вызывает vfs_read_file);
//   * не грузит DLL в свою память (вызывающий даёт base — например 0x64000000);
//   * не поддерживает REL32-релокации;
//   * не делает авто-IAT (см. dll/msdll_format.md §4 — resolve вручную).
//
// Модуль автономный: не тянет kernel.c, task.c, paging.c.
// Нужен только <stddef.h>-минимум, который тут определён локально.
#include "msdll.h"

// ============================================================
// Локальный минимум (без libc)
// ============================================================
typedef unsigned int u32;
typedef unsigned char u8;

static int str_eq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void str_cpy_n(char* dst, const char* src, int max) {
    int i = 0;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void mem_zero(void* p, u32 n) {
    u8* d = (u8*)p;
    for (u32 i = 0; i < n; i++) d[i] = 0;
}

// Проверка, что [off, off+len) внутри [0, size). Защита от битых файлов.
static int in_bounds(u32 off, u32 len, u32 size) {
    if (off > size) return 0;
    if (len > size - off) return 0;   // без переполнения
    return 1;
}

// ============================================================
// Глобальное состояние (одна копия на всё ядро)
// ============================================================
static msdll_entry_t  g_dlls[MSDLL_MAX_DLLS];
static msdll_symbol_t g_syms[MSDLL_MAX_SYMS];
static u32            g_dll_count = 0;
static u32            g_sym_count = 0;

void msdll_init(void) {
    mem_zero(g_dlls, sizeof(g_dlls));
    mem_zero(g_syms, sizeof(g_syms));
    g_dll_count = 0;
    g_sym_count = 0;
}

unsigned int msdll_symbol_count(void) { return g_sym_count; }

unsigned int msdll_dll_count(void) { return g_dll_count; }

msdll_entry_t* msdll_find(const char* name) {
    for (u32 i = 0; i < g_dll_count; i++) {
        if (str_eq(g_dlls[i].name, name)) return &g_dlls[i];
    }
    return 0;
}

// Добавить символ в таблицу. Тихо игнорирует переполнение.
static void add_symbol(const char* dll, const char* func, u32 addr) {
    if (g_sym_count >= MSDLL_MAX_SYMS) return;
    msdll_symbol_t* s = &g_syms[g_sym_count++];
    str_cpy_n(s->dll_name,  dll,  MSDLL_NAME_LEN);
    str_cpy_n(s->func_name, func, MSDLL_FUNC_LEN);
    s->addr = addr;
}

void msdll_register_export(const char* dll, const char* func, unsigned int addr) {
    if (!dll || !func || !addr) return;
    add_symbol(dll, func, addr);
}

// ============================================================
// Встроенные DLL (mskernel и т.п.)
// ============================================================
int msdll_register_builtin(const char* name,
                           const msdll_builtin_export_t* exports) {
    if (!name || !exports) return 0;
    if (msdll_find(name)) return 0;                 // уже есть
    if (g_dll_count >= MSDLL_MAX_DLLS) return 0;

    msdll_entry_t* e = &g_dlls[g_dll_count++];
    str_cpy_n(e->name, name, MSDLL_NAME_LEN);
    e->base = 0;             // встроенная — базы нет
    e->refcount = 1;
    e->loaded = 1;
    e->hdr = 0;

    for (u32 i = 0; exports[i].name != 0; i++) {
        add_symbol(name, exports[i].name, exports[i].addr);
    }
    return 1;
}

// ============================================================
// resolve
// ============================================================
unsigned int msdll_resolve(const char* dll_name, const char* func_name) {
    if (!func_name) return 0;
    int any = (!dll_name || dll_name[0] == '*' || dll_name[0] == 0);
    for (u32 i = 0; i < g_sym_count; i++) {
        if (!str_eq(g_syms[i].func_name, func_name)) continue;
        if (!any && !str_eq(g_syms[i].dll_name, dll_name)) continue;
        return g_syms[i].addr;
    }
    return 0;
}

// ============================================================
// Валидация заголовка
// ============================================================
int msdll_validate(const unsigned char* buf, unsigned int size,
                   const msdll_header_t** out_hdr) {
    if (out_hdr) *out_hdr = 0;
    if (!buf) return 0;
    if (size < sizeof(msdll_header_t)) return 0;

    const msdll_header_t* h = (const msdll_header_t*)buf;
    if (h->magic != MSDLL_MAGIC) return 0;
    if (h->version != MSDLL_VERSION) return 0;

    // Все таблицы должны лежать внутри файла.
    // (используем in_bounds, чтобы не поймать переполнение)
    if (h->export_count) {
        u32 need = h->export_count * (u32)sizeof(msdll_export_t);
        if (!in_bounds(h->export_rva, need, size)) return 0;
    }
    if (h->import_count) {
        u32 need = h->import_count * (u32)sizeof(msdll_import_t);
        if (!in_bounds(h->import_rva, need, size)) return 0;
    }
    if (h->reloc_count) {
        u32 need = h->reloc_count * (u32)sizeof(msdll_reloc_t);
        if (!in_bounds(h->reloc_rva, need, size)) return 0;
    }
    if (h->section_count) {
        u32 need = h->section_count * (u32)sizeof(msdll_section_t);
        if (!in_bounds(h->section_rva, need, size)) return 0;
    }
    // entry_rva, если задан, должен быть < image_size.
    if (h->entry_rva && h->entry_rva >= h->image_size) return 0;

    if (out_hdr) *out_hdr = h;
    return 1;
}

// ============================================================
// Загрузка из буфера
// ============================================================
// base — виртуальный адрес, по которому DLL уже лежит в памяти
// (identity: base == физический адрес). Вызывающий сам:
//   1. выделил 4 МБ (например, через pmem_alloc_block);
//   2. скопировал содержимое файла в base...base+size;
//   3. вызвал msdll_load_from_buffer.
//
// Функция НЕ копирует секции (это делает вызывающий — или оно уже в base).
// Она:
//   * валидирует;
//   * применяет релокации;
//   * регистрирует символы;
//   * вызывает DllMain (если entry_rva != 0) — как int (*)(u32, u32).
//
// Возвращает base или 0 при ошибке.
#define MSDLL_DLL_PROCESS_ATTACH 1u
#define MSDLL_DLL_PROCESS_DETACH 0u

unsigned int msdll_load_from_buffer(const char* name,
                                    const unsigned char* buf,
                                    unsigned int size,
                                    unsigned int base) {
    if (!name || !buf || !base) return 0;

    // Уже загружена — просто refcount++.
    msdll_entry_t* existing = msdll_find(name);
    if (existing) {
        existing->refcount++;
        return existing->base;
    }

    if (g_dll_count >= MSDLL_MAX_DLLS) return 0;

    const msdll_header_t* h = 0;
    if (!msdll_validate(buf, size, &h)) return 0;

    // Указатели "на диск". Все RVA — от base, а buf — по base в памяти
    // (потому что вызывающий уже скопировал файл туда). Если нет —
    // вызывающий должен был скопировать; здесь мы доверяем.
    //
    // Для удобства считаем, что buf == (const unsigned char*)base.
    // Проверить это нельзя, но так проще: reloc/strings читаются как
    // "base + rva".

    // 1. Релокации.
    // Семантика ABS32: слот хранит абсолютный адрес под MSDLL_BASE; грузим
    // по base — сдвигаем на (base - MSDLL_BASE).
    if (h->reloc_count && (h->flags & MSDLL_FLAG_RELOCS)) {
        const msdll_reloc_t* r = (const msdll_reloc_t*)(base + h->reloc_rva);
        u32 delta = base - (u32)MSDLL_BASE;
        for (u32 i = 0; i < h->reloc_count; i++) {
            if (r[i].type != MSDLL_RELOC_ABS32) continue;   // v1: только ABS32
            if (r[i].rva + 4 > h->image_size) continue;     // защита
            u32* slot = (u32*)(base + r[i].rva);
            *slot += delta;
        }
    }

    // 2. Зарегистрировать DLL.
    msdll_entry_t* e = &g_dlls[g_dll_count++];
    str_cpy_n(e->name, name, MSDLL_NAME_LEN);
    e->base = base;
    e->refcount = 1;
    e->loaded = 1;
    e->hdr = h;

    // 3. Экспорты -> таблица символов.
    if (h->export_count) {
        const msdll_export_t* ex = (const msdll_export_t*)(base + h->export_rva);
        for (u32 i = 0; i < h->export_count; i++) {
            const char* fname = (const char*)(base + ex[i].name_rva);
            u32 addr = base + ex[i].func_rva;
            add_symbol(name, fname, addr);
        }
    }

    // 4. DllMain (опционально).
    //    Сигнатура: int DllMain(u32 base, u32 reason).
    if (h->entry_rva && (h->flags & MSDLL_FLAG_ENTRY)) {
        typedef int (*dllmain_t)(unsigned int, unsigned int);
        dllmain_t entry = (dllmain_t)(base + h->entry_rva);
        entry(base, MSDLL_DLL_PROCESS_ATTACH);
    }

    return base;
}

// ============================================================
// Файловая загрузка (v2): секции по vaddr, свой аллокатор региона
// ============================================================
// Регион DLL — фиксированный диапазон виртуальных адресов, identity во
// ВСЕХ адресных пространствах (PD копируется целиком, см. paging.c).
// Внутри региона — bump-аллокатор: каждая DLL получает кусок, выровненный
// на 4 КБ, вплотную за предыдущей.
static u32 g_region_next = 0;   // смещение от msdll_region_base

// Текущая база/размер региона. По умолчанию — DLL-регион ядра.
unsigned int msdll_region_base = MSDLL_REGION_DEFAULT_BASE;
unsigned int msdll_region_size = MSDLL_REGION_DEFAULT_SIZE;

// Выравнивание вверх до 4 КБ.
static u32 align4k(u32 v) { return (v + 0xFFFu) & ~0xFFFu; }

void msdll_region_set(unsigned int base, unsigned int size) {
    msdll_region_base = base;
    msdll_region_size = size;
    g_region_next = 0;
}

void msdll_region_init(void) {
    msdll_region_base = MSDLL_REGION_DEFAULT_BASE;
    msdll_region_size = MSDLL_REGION_DEFAULT_SIZE;
    g_region_next = 0;
}

unsigned int msdll_region_alloc(unsigned int size) {
    if (size == 0) return 0;
    u32 off = align4k(g_region_next);
    u32 end = off + align4k(size);
    if (end > msdll_region_size) return 0;   // регион переполнен
    g_region_next = end;
    return msdll_region_base + off;
}

// Скопировать память побайтово (без libc).
static void mem_copy(u8* dst, const u8* src, u32 n) {
    for (u32 i = 0; i < n; i++) dst[i] = src[i];
}

// Отобразить образ из файла в память по base: скопировать каждую секцию
// из file+file_off в base+vaddr (filesz байт), хвост memsz-filesz занулить.
// Для секций, которых нет в таблице секций (section_count==0), копируем
// весь файл по смещению 0 — удобно для крошечных DLL, собранных «в лоб».
static int map_sections(const msdll_header_t* h, const u8* file,
                        u32 size, u32 base) {
    if (h->image_size == 0 || h->image_size > MSDLL_MAX_FILE) return 0;

    // Зануляем весь образ — так .bss гарантированно нулевой.
    mem_zero((void*)base, align4k(h->image_size));

    if (h->section_count == 0) {
        // Секций нет: считаем, что файл — плоский образ, кладём как есть.
        u32 n = (size < h->image_size) ? size : h->image_size;
        mem_copy((u8*)base, file, n);
        return 1;
    }

    const msdll_section_t* s = (const msdll_section_t*)(file + h->section_rva);
    for (u32 i = 0; i < h->section_count; i++) {
        u32 va = s[i].vaddr;
        u32 fsz = s[i].filesz;
        u32 msz = s[i].memsz;
        u32 foff = s[i].file_off;
        if (msz < fsz) msz = fsz;
        if (va + msz < va) return 0;              // переполнение
        if (va + msz > h->image_size) return 0;   // секция за образом
        if (fsz) {
            if (foff + fsz < foff) return 0;
            if (foff + fsz > size) return 0;      // за файлом
            mem_copy((u8*)(base + va), file + foff, fsz);
        }
        // Хвост (msz - fsz) уже нулевой (мы занулили образ).
    }
    return 1;
}

// Заголовок .msdll занимает первые 64 байта файла. Всё остальное — «образ»,
// RVA отсчитываются от начала ОБРАЗА (= file + sizeof(msdll_header_t)).
// Так решил пост-линкер tools/msdll_pack.py: заголовок не входит в образ.
#define MSDLL_IMAGE_OFF ((u32)sizeof(msdll_header_t))

// Загрузить .msdll из ПОЛНОГО ФАЙЛА (не identity). Сам выделяет базу в
// DLL-регионе, маппит секции, применяет релокации, регистрирует, зовёт DllMain.
unsigned int msdll_load_image(const char* name,
                              const unsigned char* file,
                              unsigned int size) {
    if (!name || !file || !size) return 0;
    if (size > MSDLL_MAX_FILE) return 0;
    if (size < MSDLL_IMAGE_OFF) return 0;

    msdll_entry_t* existing = msdll_find(name);
    if (existing) { existing->refcount++; return existing->base; }
    if (g_dll_count >= MSDLL_MAX_DLLS) return 0;

    const msdll_header_t* h = 0;
    if (!msdll_validate(file, size, &h)) return 0;

    // Указатель на начало образа и его длина (без заголовка).
    const u8* img  = file + MSDLL_IMAGE_OFF;
    u32       isz  = size - MSDLL_IMAGE_OFF;

    u32 base = msdll_region_alloc(h->image_size);
    if (!base) return 0;

    if (!map_sections(h, img, isz, base)) return 0;

    // Релокации ABS32 (таблица лежит в ОБРАЗЕ — читаем оттуда).
    //
    // Семантика ABS32: в слоте лежит АБСОЛЮТНЫЙ адрес, вычисленный при
    // линковке под MSDLL_BASE (0x64000000). Грузим по base — сдвигаем на
    // (base - MSDLL_BASE). Для base==MSDLL_BASE это no-op.
    if (h->reloc_count && (h->flags & MSDLL_FLAG_RELOCS)) {
        const msdll_reloc_t* r = (const msdll_reloc_t*)(img + h->reloc_rva);
        u32 delta = base - (u32)MSDLL_BASE;
        for (u32 i = 0; i < h->reloc_count; i++) {
            if (r[i].type != MSDLL_RELOC_ABS32) continue;
            if (r[i].rva + 4 > h->image_size) continue;
            u32* slot = (u32*)(base + r[i].rva);
            *slot += delta;
        }
    }

    // Регистрируем DLL.
    msdll_entry_t* e = &g_dlls[g_dll_count++];
    str_cpy_n(e->name, name, MSDLL_NAME_LEN);
    e->base = base;
    e->refcount = 1;
    e->loaded = 1;
    e->hdr = 0;   // заголовок в file — не сохраняем (буфер может уйти)

    // Экспорты -> таблица символов. Таблица — в ОБРАЗЕ, адреса — в base.
    if (h->export_count) {
        const msdll_export_t* ex = (const msdll_export_t*)(img + h->export_rva);
        for (u32 i = 0; i < h->export_count; i++) {
            const char* fname = (const char*)(img + ex[i].name_rva);
            add_symbol(name, fname, base + ex[i].func_rva);
        }
    }

    // DllMain (если есть).
    if (h->entry_rva && (h->flags & MSDLL_FLAG_ENTRY)) {
        typedef int (*dllmain_t)(unsigned int, unsigned int);
        dllmain_t entry = (dllmain_t)(base + h->entry_rva);
        entry(base, MSDLL_DLL_PROCESS_ATTACH);
    }

    return base;
}

// Загрузить .msdll с диска. vfs_read_file объявлена weak — если её нет
// (например, при хостовом тесте), функция вернёт 0, не ломая линковку.
__attribute__((weak))
int vfs_read_file(const char* path, unsigned char* buf, unsigned int max);

#define MSDLL_FILE_BUF  0x07800000u   // временный буфер под файл (2 МБ)

unsigned int msdll_load_file(const char* name, const char* path) {
    if (!name || !path) return 0;

    msdll_entry_t* existing = msdll_find(name);
    if (existing) { existing->refcount++; return existing->base; }

    if (!vfs_read_file) return 0;

    // Читаем файл во временный буфер в верхней RAM (не трогаем регион).
    unsigned char* buf = (unsigned char*)MSDLL_FILE_BUF;
    int n = vfs_read_file(path, buf, MSDLL_MAX_FILE);
    if (n <= 0) return 0;

    // msdll_load_image копирует секции и таблицы из buf — buf можно
    // переиспользовать после возврата (заголовок hdr НЕ сохраняем как
    // указатель на buf: e->hdr указывает в buf, но для resolve он не нужен;
    // чтобы не было висячего указателя — обнулим e->hdr ниже).
    unsigned int base = msdll_load_image(name, buf, (unsigned int)n);
    if (base) {
        msdll_entry_t* e = msdll_find(name);
        if (e) e->hdr = 0;   // buf больше не валиден
    }
    return base;
}
