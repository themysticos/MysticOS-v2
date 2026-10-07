// libc/unistd.h - заглушка POSIX-функций.
#ifndef LIBC_UNISTD_H
#define LIBC_UNISTD_H

typedef unsigned int size_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int    usleep(unsigned int us);
unsigned int sleep(unsigned int sec);
int    read(int fd, void* buf, size_t n);
int    write(int fd, const void* buf, size_t n);
int    close(int fd);
int    isatty(int fd);
int    getpid(void);
int    unlink(const char* path);
int    mkdir(const char* path, int mode);
int    rmdir(const char* path);
char*  getcwd(char* buf, size_t n);

#endif
