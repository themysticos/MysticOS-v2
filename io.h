// io.h - порты ввода-вывода
#ifndef IO_H
#define IO_H
void outb(unsigned short port, unsigned char v);
unsigned char inb(unsigned short port);
void outw(unsigned short port, unsigned short v);
unsigned short inw(unsigned short port);
void outl(unsigned short port, unsigned int v);
unsigned int inl(unsigned short port);
#endif
