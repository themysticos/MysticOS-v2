// fs.h - единый API виртуальной ФС MysticOS 2.
//
// VFS стоит между ядром/API и конкретным драйвером ФС (сейчас FAT16).
// Драйвер регистрируется в таблице vfs.c (fs_driver_t). Добавление новой ФС
// = написать драйвер + зарегистрировать его; код ядра НЕ меняется.
//
// Адаптировано из MysticOS 1 (vfs.c/fs.h), но:
//   - без MSFS и без cwd (в M2 API путь-ориентированный);
//   - через таблицу драйверов (vtable), а не if-цепочки.
#ifndef FS_H
#define FS_H

typedef enum { FS_UNKNOWN = 0, FS_FAT12, FS_FAT16, FS_FAT32, FS_MSFS } fs_type_t;

#define FS_PATH_MAX 128

// ------------------------------------------------------------
// Интерфейс драйвера ФС. Каждая ФС реализует эти функции.
// Пути: абсолютные ("/dir/file") или относительные; разделитель '/'.
// ------------------------------------------------------------
typedef struct fs_driver_s {
    const char* name;
    fs_type_t   type;
    int (*probe)(void);                        // 1 если ФС найдена на диске
    int (*find)(const char* path);             // 1/0
    int (*read_dir)(const char* path, char* names, int* is_dir,
                    unsigned int* sizes, int max);
    int (*read_file)(const char* path, unsigned char* buf, unsigned int max);
    int (*size)(const char* path);             // размер или -1
    int (*mkdir)(const char* path);            // 1 при успехе
    int (*write)(const char* path, const char* content);
} fs_driver_t;

// --- Инициализация/состояние ---
void vfs_init(void);
fs_type_t vfs_get_type(void);
const char* vfs_type_name(void);

// --- Операции (путь-ориентированные, как API v2) ---
int  vfs_find(const char* path);
int  vfs_read_dir(const char* path, char* names, int* is_dir,
                  unsigned int* sizes, int max);
int  vfs_read_file(const char* path, unsigned char* buf, unsigned int max);
int  vfs_size(const char* path);
int  vfs_mkdir(const char* path);
int  vfs_write(const char* path, const char* content);

// Печать содержимого каталога (для терминала, как vfs_ls в MysticOS 1).
void vfs_list_print(const char* path);

// Зарегистрировать драйвер (для будущих ФС).
int  vfs_register(const fs_driver_t* drv);

// Нормализация пути ('.', '..', '//') в out (FS_PATH_MAX). Полезно и вне VFS.
void vfs_normalize(const char* path, char* out);

// Разбор префикса диска "D0:"/"D1:". Без префикса — диск 0.
// В *drive кладётся номер диска, в out — путь БЕЗ префикса (нормализованный).
// Пример: "D1:/dir/f" -> drive=1, out="/dir/f"; "/dir/f" -> drive=0.
void vfs_resolve_drive(const char* path, int* drive, char* out);

#endif // FS_H
