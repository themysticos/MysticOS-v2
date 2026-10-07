// msdll.h - формат .msdll (DLL для MysticOS 2) и API загрузчика.
//
// ВАЖНО: это НЕ PE. Свой формат, свой загрузчик, свой ABI.
// Полное описание — в dll/msdll_format.md.
//
// Документ и код — отдельный модуль. Ядро (kernel.c, task.c, paging.c)
// его НЕ включает, пока ты сам не решишь встроить.
#ifndef MSDLL_H
#define MSDLL_H

// ============================================================
// 1. Идентификация файла
// ============================================================
// Магия — 4 байта 'M','S','D','L' в little-endian: 0x4C44534D.
#define MSDLL_MAGIC    0x4C44534Du
#define MSDLL_VERSION  1

// Флаги в msdll_header_t.flags
#define MSDLL_FLAG_RELOCS  0x1u   // есть таблица релокаций
#define MSDLL_FLAG_ENTRY   0x2u   // есть точка входа (DllMain)

// Флаги в msdll_section_t.flags
#define MSDLL_SEC_R  0x1u
#define MSDLL_SEC_W  0x2u
#define MSDLL_SEC_X  0x4u

// Тип релокации
#define MSDLL_RELOC_ABS32  1u     // *(u32*)(base+rva) += base
// 2 = REL32 не поддерживаем в v1

// ============================================================
// 2. Структуры на диске (все packed, little-endian)
// ============================================================

typedef struct {
    unsigned int  magic;          // MSDLL_MAGIC
    unsigned int  version;        // MSDLL_VERSION
    unsigned int  flags;          // MSDLL_FLAG_*
    unsigned int  image_size;     // полный размер образа в памяти
    unsigned int  entry_rva;      // RVA точки входа (DllMain) или 0
    unsigned int  export_count;
    unsigned int  export_rva;
    unsigned int  import_count;
    unsigned int  import_rva;
    unsigned int  reloc_count;
    unsigned int  reloc_rva;
    unsigned int  section_count;
    unsigned int  section_rva;
    unsigned int  reserved[3];    // до 64 байт
} __attribute__((packed)) msdll_header_t;

typedef struct {
    unsigned int name_rva;   // RVA строки с именем (ASCIIZ)
    unsigned int func_rva;   // RVA функции от базы DLL
} __attribute__((packed)) msdll_export_t;

typedef struct {
    unsigned int dll_name_rva;   // RVA строки с именем DLL
    unsigned int func_name_rva;  // RVA строки с именем функции
    unsigned int iat_rva;        // RVA слота в IAT программы
} __attribute__((packed)) msdll_import_t;

typedef struct {
    unsigned int rva;    // где патчить (RVA от базы DLL)
    unsigned int type;   // MSDLL_RELOC_*
} __attribute__((packed)) msdll_reloc_t;

typedef struct {
    unsigned int vaddr;      // RVA от базы DLL
    unsigned int filesz;     // размер в файле
    unsigned int memsz;      // размер в памяти (>= filesz)
    unsigned int flags;      // MSDLL_SEC_*
    unsigned int file_off;   // смещение в файле
} __attribute__((packed)) msdll_section_t;

// ============================================================
// 3. Runtime-состояние (не на диске)
// ============================================================

#define MSDLL_MAX_DLLS   8
#define MSDLL_MAX_SYMS  256
#define MSDLL_NAME_LEN   32
#define MSDLL_FUNC_LEN   48

typedef struct {
    char          name[MSDLL_NAME_LEN];   // "mskernel", "msgui"
    unsigned int  base;                    // виртуальная база (identity)
    unsigned int  refcount;
    int           loaded;                  // 1 = загружена
    const msdll_header_t* hdr;             // указатель на заголовок в памяти
} msdll_entry_t;

typedef struct {
    char          dll_name[MSDLL_NAME_LEN];
    char          func_name[MSDLL_FUNC_LEN];
    unsigned int  addr;
} msdll_symbol_t;

// Встроенный экспорт (mskernel.dll — таблица в ядре, не файл).
typedef struct {
    const char*   name;
    unsigned int  addr;
} msdll_builtin_export_t;

// ============================================================
// 4. API загрузчика (реализация — msdll.c)
// ============================================================

// Инициализация подсистемы (обнуляет таблицы). Звать один раз при старте.
void msdll_init(void);

// Зарегистрировать встроенную DLL (например "mskernel").
// exports — массив, заканчивающийся { 0, 0 }.
// Зарегистрировать ОДИН экспорт (для PE-DLL: имя функции -> адрес).
void msdll_register_export(const char* dll, const char* func, unsigned int addr);

int  msdll_register_builtin(const char* name,
                            const msdll_builtin_export_t* exports);

// Найти символ. Возвращает адрес или 0.
// dll_name == 0 или "*" — искать во всех загруженных DLL.
unsigned int msdll_resolve(const char* dll_name, const char* func_name);

// Проверить, что буфер — валидный .msdll. Возвращает 1/0.
// Если ok — *out_hdr указывает на заголовок внутри буфера.
int  msdll_validate(const unsigned char* buf, unsigned int size,
                    const msdll_header_t** out_hdr);

// Загрузить DLL из уже прочитанного буфера по заданной базе.
// Применяет релокации, регистрирует символы, вызывает DllMain (если есть).
// Возвращает базу или 0 при ошибке. НЕ читает диск — читает вызывающий.
unsigned int msdll_load_from_buffer(const char* name,
                                    const unsigned char* buf,
                                    unsigned int size,
                                    unsigned int base);

// Найти DLL по имени в таблице. Возвращает указатель или 0.
msdll_entry_t* msdll_find(const char* name);

// Сколько символов зарегистрировано (для отладки).
unsigned int msdll_symbol_count(void);

// Сколько DLL загружено (для отладки).
unsigned int msdll_dll_count(void);

// ============================================================
// 5. Файловая загрузка (v2)
// ============================================================
// База DLL-региона (PDE[25]). Identity во ВСЕХ задачах (PD копируется
// целиком), поэтому адрес одинаков и в ядре, и в ring3.
// Значения по умолчанию; тест на хосте задаёт свои через msdll_region_set().
#define MSDLL_REGION_DEFAULT_BASE  0x0C000000u   // 192 MB (inside 256MB RAM!)
#define MSDLL_REGION_DEFAULT_SIZE  0x01000000u   // 16 МБ (4 PSE-блока, зарезервированы в pmem)
#define MSDLL_MAX_FILE             0x00300000u   // макс. размер .msdll (3 МБ)

// Текущая база/размер региона (меняются msdll_region_set для тестов).
extern unsigned int msdll_region_base;
extern unsigned int msdll_region_size;

// Задать регион явно (для хостового теста). В ядре не нужно —
// msdll_region_init() выставит значения по умолчанию.
void msdll_region_set(unsigned int base, unsigned int size);

// Обнулить bump-аллокатор региона (звать один раз при старте).
// Выставляет регион в значения по умолчанию (0x64000000, 4 МБ).
void msdll_region_init(void);

// Выделить кусок DLL-региона под образ size байт (с выравниванием на 4 КБ).
// Возвращает базу или 0. Внутренний аллокатор региона (bump).
unsigned int msdll_region_alloc(unsigned int size);

// Загрузить DLL из ФАЙЛА. Читает диск через vfs_read_file (нужен vfs.h).
// Имя name — под каким зарегистрировать ("msgui"). path — путь на диске.
// Возвращает базу или 0. Если уже загружена — refcount++ и вернуть базу.
//
// ТРЕБУЕТ: vfs_init() уже вызван, DLL-регион не пересекается с ядром.
unsigned int msdll_load_file(const char* name, const char* path);

// Загрузить из буфера ПОЛНОГО файла (не identity): копирует секции по vaddr.
// Отличается от msdll_load_from_buffer (тот ждёт, что файл уже лежит по base).
// Сам выделяет базу в DLL-регионе. Возвращает базу или 0.
unsigned int msdll_load_image(const char* name,
                              const unsigned char* file,
                              unsigned int size);

// ============================================================
// 6. Макросы для АВТОРОВ DLL (не для ядра)
// ============================================================
// Использование в .c файле DLL:
//
//   #include "msdll.h"
//
//   int my_window_create(const char* t, int x, int y, int w, int h) {
//       ...
//       return 1;
//   }
//
//   MSDLL_EXPORT(window_create, my_window_create);
//
// Разворачивается в запись в секции .msdll_exports:
//   { name_rva = адрес строки, func_rva = адрес функции }
//
// Имя строки идёт в отдельную секцию .msdll_ropool (чтобы не мешать
// таблице). Линкер и пост-линкер считают RVA от базы 0x64000000.
// База, под которую линкуется DLL (dll/msdll.ld: . = 0x64000000).
// Поля таблиц экспортов/импортов хранят RVA = адрес - MSDLL_BASE.
#define MSDLL_BASE 0x64000000u

#define MSDLL_EXPORT(export_name, fn)                                   \
    static const char _msdll_expname_##fn[]                             \
        __attribute__((section(".msdll_ropool"), used)) = #export_name; \
    __attribute__((section(".msdll_exports"), used))                   \
    const msdll_export_t _msdll_exp_##fn = {                            \
        (unsigned int)((unsigned long)_msdll_expname_##fn - MSDLL_BASE),\
        (unsigned int)((unsigned long)&fn - MSDLL_BASE)                 \
    }

#endif // MSDLL_H
