// libc/fcntl.h - заглушка (Doom использует O_NONBLOCK и т.п.).
#ifndef LIBC_FCNTL_H
#define LIBC_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0100
#define O_NONBLOCK 04000

#endif
