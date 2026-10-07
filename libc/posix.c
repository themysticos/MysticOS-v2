// libc/posix.c - POSIX-заглушки (для Doom и совместимости).
#include "../include/ms/system.h"

typedef unsigned int size_t;

int usleep(unsigned int us) { ms_sleep(us / 1000); return 0; }
unsigned int sleep(unsigned int sec) { ms_sleep(sec * 1000); return 0; }
int read(int fd, void* buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
int write(int fd, const void* buf, size_t n) { (void)fd; (void)buf; (void)n; return (int)n; }
int close(int fd) { (void)fd; return 0; }
int isatty(int fd) { (void)fd; return 0; }
int getpid(void) { return 0; }
int unlink(const char* path) { (void)path; return -1; }
int mkdir(const char* path, int mode) { (void)path; (void)mode; return -1; }
int rmdir(const char* path) { (void)path; return -1; }
char* getcwd(char* buf, size_t n) { (void)n; if (buf) buf[0] = '/'; if (buf && n > 1) buf[1] = 0; return buf; }

#include "../include/libc/sys/time.h"
#include "../include/libc/sys/mman.h"
#include "../include/libc/termios.h"

// sys/time.h
int gettimeofday(struct timeval* tv, struct timezone* tz) {
    (void)tz;
    if (tv) { unsigned int t = ms_ticks(); tv->tv_sec = t / 100; tv->tv_usec = (t % 100) * 10000; }
    return 0;
}

// sys/mman.h
void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
    (void)len; (void)prot; (void)flags; (void)fd; (void)off; return addr;
}
int munmap(void* addr, size_t len) { (void)addr; (void)len; return 0; }

// sys/ioctl.h
int ioctl(int fd, unsigned long req, ...) { (void)fd; (void)req; return -1; }

// termios.h
struct termios; int tcgetattr(int fd, struct termios* t) { (void)fd; (void)t; return 0; }
int tcsetattr(int fd, int act, const struct termios* t) { (void)fd; (void)act; (void)t; return 0; }

// assert.h
void __libc_assert_fail(const char* expr, const char* file, int line) {
    (void)expr; (void)file; (void)line;
    ms_exit(1);
}

// time.h
typedef long time_t;
time_t time(time_t* t) { time_t v = (time_t)(ms_ticks() / 100); if (t) *t = v; return v; }
