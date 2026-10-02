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
    if (h->reloc_count && (h->flags & MSDLL_FLAG_RELOCS)) {
        const msdll_reloc_t* r = (const msdll_reloc_t*)(base + h->reloc_rva);
        for (u32 i = 0; i < h->reloc_count; i++) {
            if (r[i].type != MSDLL_RELOC_ABS32) continue;   // v1: только ABS32
            if (r[i].rva + 4 > h->image_size) continue;     // защита
            u32* slot = (u32*)(base + r[i].rva);
            *slot += base;
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
