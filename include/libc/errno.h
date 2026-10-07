// libc/errno.h - коды ошибок.
#ifndef LIBC_ERRNO_H
#define LIBC_ERRNO_H

int* __errno_location(void);
#define errno (*__errno_location())

#define EPERM  1
#define ENOENT 2
#define EINTR  4
#define EIO    5
#define EBADF  9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EINVAL 22
#define ENOTDIR 20
#define EISDIR 21
#define ENOSPC 28
#define EEXIST 17
#define ENOTEMPTY 39
#define EROFS 30

#endif
