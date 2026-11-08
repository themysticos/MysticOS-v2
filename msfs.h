// msfs.h - MSFS v2 (Mystic Soft File System) для MysticOS 2.
//
// Формат: МАРКЕРНАЯ ФС (LBA 0 = суперблок).
//   Блоки 4 КБ: [START] name|datalen|<data> [END] next_lba
//   Свободные: [FREE_START] ... [FREE_END] (цепочка free-списка).
//   Суперблок хранит free_lba (курсор) и free_list_head (голова free-списка).
//   Удаление: маркеры файла -> FREE_*, цепочка встраивается в free-список.
//   Шифрование/CoW/журнал — НЕ реализованы (отменены).
#ifndef MSFS_H
#define MSFS_H

void msfs_use_drive(int d);
int  msfs_current_drive(void);

// Определить MSFS2 на текущем диске: прочитать суперблок, отсканировать
// блоки до free_lba, собрать индекс в памяти. 1/0.
int  msfs_probe(void);

// Форматировать текущий диск как MSFS v2 (пустая ФС).
void msfs_format(void);

// Операции (пути от корня, разделитель '/').
int  msfs_find(const char* path);                              // 1/0
int  msfs_is_dir_inode(int inode);
int  msfs_read_dir(const char* path, char* names, int* is_dir,
                   unsigned int* sizes, int max);
int  msfs_read_file(const char* path, unsigned char* buf, unsigned int max);
// Размер файла в байтах или -1 (не файл / не найден).
int  msfs_size(const char* path);
int  msfs_mkdir(const char* path);                            // 1 при успехе
int  msfs_write(const char* path, const char* content);       // 1 при успехе
int  msfs_delete(const char* path);                           // 1 при успехе (файлы)
void msfs_list_print(const char* path);                       // печать (терминал)

#endif // MSFS_H
