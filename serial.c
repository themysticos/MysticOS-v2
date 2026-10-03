// serial.c - COM1 UART 16550 (polling, без IRQ).
#include "serial.h"
#include "io.h"

#define COM1  0x3F8

// Регистры UART (offset от COM1):
//   +0 (DLAB=0)  THR/RBR   — data
//   +0 (DLAB=1)  DLL       — divisor low
//   +1 (DLAB=0)  IER       — interrupt enable
//   +1 (DLAB=1)  DLM       — divisor high
//   +2           FCR/IIR
//   +3           LCR
//   +4           MCR
//   +5           LSR       — line status (bit 5 = THR empty)

void serial_init(void) {
    outb(COM1 + 1, 0x00);   // disable interrupts
    outb(COM1 + 3, 0x80);   // DLAB=1
    outb(COM1 + 0, 0x03);   // divisor low  = 3 (38400 baud)
    outb(COM1 + 1, 0x00);   // divisor high = 0
    outb(COM1 + 3, 0x03);   // 8N1
    outb(COM1 + 2, 0xC7);   // FIFO: enable, clear, 14-byte threshold
    outb(COM1 + 4, 0x0B);   // MCR: DTR, RTS, OUT2 (IRQ enable)
}

static int serial_tx_ready(void) {
    return (inb(COM1 + 5) & 0x20) != 0;   // THR empty
}

void serial_putc(char c) {
    // Ждём, пока в transmit-буфере освободится место.
    for (unsigned int i = 0; i < 1000000; i++) {
        if (serial_tx_ready()) break;
    }
    outb(COM1, (unsigned char)c);
}

void serial_print(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_print_int(int v) {
    if (v == 0) { serial_putc('0'); return; }
    if (v < 0) { serial_putc('-'); v = -v; }
    char buf[12]; int i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i) serial_putc(buf[--i]);
}

void serial_print_hex(unsigned int v) {
    static const char h[] = "0123456789ABCDEF";
    serial_print("0x");
    for (int i = 28; i >= 0; i -= 4) serial_putc(h[(v >> i) & 0xF]);
}

void serial_kv(const char* key, unsigned int value) {
    serial_print(key);
    serial_print(" = ");
    serial_print_hex(value);
    serial_print("\n");
}
