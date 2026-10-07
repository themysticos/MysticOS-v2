// msfs.h - MSFS v2 (Mystic Soft File System) для MysticOS 2.
//
// НОВЫЙ формат (см. msfs2_format.md):
//   * ВСЯ ФС зашифрована (свой блочный шифр, XTEA-подобный).
//   * Ключ разбросан по первым 59 секторам (START/данные/END вперемешку),
//     собирается по порядку появления маркеров START.
//   * CoW + журнал + снапшоты (этапы 2-3).
//
// Старая шуточная MSFS v1 (bit-reverse, XOR имён) БОЛЬШЕ НЕ ПОДДЕРЖИВАЕТСЯ.
#ifndef MSFS_H
#define MSFS_H

void msfs_use_drive(int d);
int  msfs_current_drive(void);

// Определить MSFS v2 на текущем диске: собрать ключ, расшифровать суперблок,
// загрузить метаданные. 1/0.
int  msfs_probe(void);

// Форматировать текущий диск как MSFS v2 (создать пустую ФС, разложить ключ).
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
void msfs_list_print(const char* path);                       // печать (терминал)

#endif // MSFS_H
