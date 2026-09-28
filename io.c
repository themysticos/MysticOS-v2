// io.c - порты ввода-вывода
#include "io.h"

void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
void outw(unsigned short port, unsigned short v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}
unsigned short inw(unsigned short port) {
    unsigned short r;
    __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
