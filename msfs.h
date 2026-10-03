// msfs.h - MSFS (Mystic Soft File System) для MysticOS 2.
//
// Собственная ФС: суперблок + таблица inode + цепочки кластеров (next[]).
// Каталоги — inode type=2, дети находятся сканированием по parent.
//
// Порт из MysticOS 1 (msfs.c), адаптирован под M2:
//   - per-drive (D0/D1), как FAT;
//   - путь-ориентированный API (без cwd/base);
//   - msfs_read_dir (массивы) для VFS/эксплорера.
#ifndef MSFS_H
#define MSFS_H

// Выбрать диск ATA (0/1) для последующих операций MSFS.
void msfs_use_drive(int d);
int  msfs_current_drive(void);

// Определить MSFS на текущем диске и загрузить inode-таблицу. 1/0.
int  msfs_probe(void);

// Форматировать текущий диск как MSFS (создать пустую ФС).
void msfs_format(void);

// Операции (пути абсолютные/относительные от корня; разделитель '/').
int  msfs_find(const char* path);                              // 1/0
int  msfs_is_dir_inode(int inode);
int  msfs_read_dir(const char* path, char* names, int* is_dir,
                   unsigned int* sizes, int max);
int  msfs_read_file(const char* path, unsigned char* buf, unsigned int max);
int  msfs_mkdir(const char* path);                            // 1 при успехе
int  msfs_write(const char* path, const char* content);       // 1 при успехе
void msfs_list_print(const char* path);                       // печать (терминал)

#endif // MSFS_H
