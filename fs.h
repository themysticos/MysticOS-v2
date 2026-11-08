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
    int (*write_bin)(const char* path, const unsigned char* content, unsigned int len);
    // --- Файловый дескриптор (стриминг: open/read/seek/close) ---
    // fh — внутреннее состояние драйвера (см. fat_file_t).
    // Возвращают: open -> 1/0; read -> число байт; seek -> новая позиция; close -> void.
    int (*file_open)(const char* path, void* fh);
    int (*file_read)(void* fh, unsigned char* buf, unsigned int n);
    int (*file_seek)(void* fh, unsigned int pos);
    void (*file_close)(void* fh);
} fs_driver_t;

// Максимальный размер состояния файла (драйвер кладёт своё).
#define FS_FH_MAX 256

// Публичный файловый дескриптор: ФС + состояние драйвера.
// Программам не нужен — ядро хранит массив таких.
typedef struct {
    int used;
    int drive;
    unsigned int pos;          // текущая позиция (байт)
    unsigned int size;         // размер файла
    char path[FS_PATH_MAX];
    unsigned char fh[FS_FH_MAX];   // внутреннее состояние драйвера
} fs_file_t;

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
// Бинарная запись (len явно, внутри могут быть нули). Возвращает 1/0.
int  vfs_write_bin(const char* path, const unsigned char* content, unsigned int len);

// --- Файловый дескриптор (стриминг) ---
// vfs_file_open: открыть файл, вернуть индекс дескриптора (>=0) или -1.
// vfs_file_read: прочитать до n байт с текущей позиции; вернуть число байт.
// vfs_file_seek: установить позицию; вернуть новую позицию или -1.
// vfs_file_close: закрыть дескриптор.
// vfs_file_size: размер открытого файла или -1.
int  vfs_file_open(const char* path);
int  vfs_file_read(int fd, unsigned char* buf, unsigned int n);
int  vfs_file_seek(int fd, unsigned int pos);
void vfs_file_close(int fd);
int  vfs_file_size(int fd);

// Печать содержимого каталога (для терминала, как vfs_ls в MysticOS 1).
void vfs_list_print(const char* path);

// Зарегистрировать драйвер (для будущих ФС).
int  vfs_register(const fs_driver_t* drv);

// Нормализация пути ('.', '..', '//') в out (FS_PATH_MAX). Полезно и вне VFS.
void vfs_normalize(const char* path, char* out);

// Есть ли на диске d распознанная ФС (драйвер определён). 1/0.
int vfs_drive_has_fs(int d);

// Разбор префикса диска "D0:"/"D1:". Без префикса — диск 0.
// В *drive кладётся номер диска, в out — путь БЕЗ префикса (нормализованный).
// Пример: "D1:/dir/f" -> drive=1, out="/dir/f"; "/dir/f" -> drive=0.
void vfs_resolve_drive(const char* path, int* drive, char* out);

#endif // FS_H
