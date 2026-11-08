// ms/file.h - файловый дескриптор (стриминг) для программ MysticOS.
//
// Позволяет читать большие файлы ПОРЦИЯМИ, не загружая целиком в память.
// Классический open/read/seek/close.
#ifndef MS_FILE_H
#define MS_FILE_H

#include "syscall.h"

// Открыть файл. Возвращает дескриптор (>=0) или -1.
static inline int ms_file_open(const char* path) {
    return __ms_sc1(SYS_FILE_OPEN, (unsigned int)path);
}

// Прочитать до n байт с текущей позиции. Возвращает число прочитанных байт
// (0 = конец файла) или -1 при ошибке.
static inline int ms_file_read(int fd, void* buf, unsigned int n) {
    return __ms_sc3(SYS_FILE_READ, (unsigned int)fd,
                    (unsigned int)buf, n);
}

// Установить позицию (байт от начала). Возвращает новую позицию или -1.
static inline int ms_file_seek(int fd, unsigned int pos) {
    return __ms_sc2(SYS_FILE_SEEK, (unsigned int)fd, pos);
}

// Размер открытого файла или -1.
static inline int ms_file_size(int fd) {
    return __ms_sc1(SYS_FILE_SIZE, (unsigned int)fd);
}

// Закрыть дескриптор.
static inline void ms_file_close(int fd) {
    __ms_sc1(SYS_FILE_CLOSE, (unsigned int)fd);
}

#endif // MS_FILE_H
