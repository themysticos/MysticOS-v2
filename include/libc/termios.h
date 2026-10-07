// libc/termios.h - заглушка.
#ifndef LIBC_TERMIOS_H
#define LIBC_TERMIOS_H
struct termios { unsigned int c_iflag, c_oflag, c_cflag, c_lflag; unsigned char c_cc[32]; };
#define ECHO 8
#define TCSAFLUSH 2
int tcgetattr(int fd, struct termios* t);
int tcsetattr(int fd, int act, const struct termios* t);
#endif
