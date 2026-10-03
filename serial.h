// serial.h - COM1 UART 16550 для отладки (polling, без IRQ).
#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_print(const char* s);
void serial_print_int(int v);
void serial_print_hex(unsigned int v);
void serial_kv(const char* key, unsigned int value);

#endif // SERIAL_H
