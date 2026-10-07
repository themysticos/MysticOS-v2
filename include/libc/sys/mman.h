// libc/sys/mman.h - заглушка.
#ifndef LIBC_SYS_MMAN_H
#define LIBC_SYS_MMAN_H
typedef unsigned int size_t;
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_FAILED ((void*)-1)
void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off);
int munmap(void* addr, size_t len);
#endif
