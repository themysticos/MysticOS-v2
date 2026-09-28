// ata.c - драйвер ATA PIO (LBA28), два диска, таймауты.
#include "ata.h"
#include "io.h"

static int g_drive = 0;

void ata_select(int drive) { g_drive = drive & 1; }
int ata_current(void) { return g_drive; }

void ata_read_sector(unsigned int lba, unsigned char* buffer) {
    for (unsigned int w = 0; inb(0x1F7) & 0x80; w++) if (w > 1000000u) return;

    outb(0x1F6, 0xE0 | (g_drive << 4) | ((lba >> 24) & 0x0F));
    inb(0x3F6); inb(0x3F6); inb(0x3F6); inb(0x3F6);
    outb(0x1F2, 1);
    outb(0x1F3, (unsigned char)lba);
    outb(0x1F4, (unsigned char)(lba >> 8));
    outb(0x1F5, (unsigned char)(lba >> 16));
    outb(0x1F7, 0x20);

    for (unsigned int t = 0; ; t++) {
        unsigned char st = inb(0x1F7);
        if (!(st & 0x80) && (st & 0x08)) break;
        if (st & 0x01) return;
        if (t >= 1000000u) return;
    }
    for (int i = 0; i < 256; i++) {
        unsigned short d = inw(0x1F0);
        buffer[i * 2] = d & 0xFF;
        buffer[i * 2 + 1] = (d >> 8) & 0xFF;
    }
}

void ata_write_sector(unsigned int lba, const unsigned char* buffer) {
    for (unsigned int w = 0; inb(0x1F7) & 0x80; w++) if (w > 1000000u) return;

    outb(0x1F6, 0xE0 | (g_drive << 4) | ((lba >> 24) & 0x0F));
    inb(0x3F6); inb(0x3F6); inb(0x3F6); inb(0x3F6);
    outb(0x1F2, 1);
    outb(0x1F3, (unsigned char)lba);
    outb(0x1F4, (unsigned char)(lba >> 8));
    outb(0x1F5, (unsigned char)(lba >> 16));
    outb(0x1F7, 0x30);

    for (unsigned int t = 0; ; t++) {
        unsigned char st = inb(0x1F7);
        if (!(st & 0x80) && (st & 0x08)) break;
        if (st & 0x01) return;
        if (t >= 1000000u) return;
    }
    for (int i = 0; i < 256; i++) {
        outw(0x1F0, buffer[i * 2] | (buffer[i * 2 + 1] << 8));
    }
    for (unsigned int t = 0; ; t++) {
        if (!(inb(0x1F7) & 0x80)) break;
        if (t >= 1000000u) return;
    }
}
