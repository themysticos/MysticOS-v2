// fat.h — путь-ориентированный API драйвера FAT12/FAT16 для MysticOS
#ifndef FAT_H
#define FAT_H

// Все функции принимают текущий каталог как base_dir:
//   0    — корневой каталог (FAT16, фиксированная область)
//   >0   — первый кластер подкаталога
// Пути могут быть абсолютными ("/dir/file") или относительными.

// Выбрать диск ATA (0/1) для последующих операций FAT.
// Также выставляет ata_select. По умолчанию — диск 0.
void fat_use_drive(int d);
int  fat_current_drive(void);

// Форматировать текущий диск как FAT заданной версии.
// version: 12, 16 или 32 (сейчас поддерживается 16). Возвращает 1 при успехе.
int  fat_format(int version);

int  fat_find(int base_dir, const char* path);      // 1 если найден, 0 если нет
void fat_list(int base_dir, const char* path);      // печатает содержимое каталога
void fat_read(int base_dir, const char* path);      // печатает содержимое файла
void fat_write_path(int base_dir, const char* path, const char* content);
// Бинарная запись: len задаётся явно (внутри могут быть нули). 1 при успехе.
int  fat_write_path_bin(int base_dir, const char* path,
                        const unsigned char* content, unsigned int len);
int  fat_mkdir(int base_dir, const char* path);     // 1 при успехе
int  fat_cd(const char* path, unsigned int* out_cluster); // абсолютный путь; 1 при успехе

// Заполняет массивы списком каталога (для графического эксплорера).
// names — буфер размером max*13 (12 симв. имя + '\0'), is_dir — 0/1, sizes — размеры.
// Возвращает число записей (до max).
int  fat_read_dir(int base_dir, const char* path, char* names, int* is_dir,
                  unsigned int* sizes, int max);

// Читает весь файл в буфер. Возвращает число байт или -1.
int  fat_read_file_buf(const char* path, unsigned char* buf, unsigned int max);

// --- Файловый дескриптор (стриминг). fh — указатель на структуру драйвера
//     (fat_file_state_t), размер <= FS_FH_MAX. Возвраты: open->1/0,
//     read->число байт, seek->новая позиция, close->void. ---
int  fat_file_open(const char* path, void* fh);
int  fat_file_read(void* fh, unsigned char* buf, unsigned int n);
int  fat_file_seek(void* fh, unsigned int pos);
void fat_file_close(void* fh);

#endif
