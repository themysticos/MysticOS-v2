// ms/fs.h - файловые операции для программ MysticOS.
#ifndef MS_FS_H
#define MS_FS_H

#include "syscall.h"

// Размер файла в байтах или -1.
static inline int ms_fs_size(const char* path) {
    return __ms_sc1(SYS_FS_SIZE, (unsigned int)path);
}

// Существует ли файл/каталог (1/0).
static inline int ms_fs_exists(const char* path) {
    return __ms_sc1(SYS_FS_EXISTS, (unsigned int)path);
}

// Прочитать файл целиком. Возвращает число байт или -1.
static inline int ms_fs_read(const char* path, void* buf, unsigned int max) {
    return __ms_sc3(SYS_FS_READ, (unsigned int)path,
                    (unsigned int)buf, max);
}

// Записать файл (строка до '\0'). Перезапись. 1/0.
static inline int ms_fs_write(const char* path, const char* content) {
    return __ms_sc2(SYS_FS_WRITE, (unsigned int)path, (unsigned int)content);
}

// Список каталога:
//   names  - буфер max*13 (12 симв + '\0')
//   isdir  - max int (0/1)
//   sizes  - max unsigned int (размер)
//   max    - сколько максимум записей
// Возвращает число записей или -1.
static inline int ms_fs_list(const char* path, char* names, int* isdir,
                             unsigned int* sizes, int max) {
    return __ms_sc5(SYS_FS_LIST, (unsigned int)path, (unsigned int)names,
                    (unsigned int)isdir, (unsigned int)sizes, (unsigned int)max);
}

// Форматировать диск. drive=0/1, fs=0(FAT)/1(MSFS), version=12/16/32. 1/0.
static inline int ms_format(int drive, int fs, int version) {
    return __ms_sc3(SYS_FORMAT, (unsigned int)drive, (unsigned int)fs, (unsigned int)version);
}

#endif
