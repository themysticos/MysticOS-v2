// ata.c - драйвер ATA PIO (LBA28), до 4 устройств (2 канала × 2).
//
//   drive 0 = primary master    (IO 0x1F0, ctrl 0x3F6, select 0xA0)
//   drive 1 = primary slave     (IO 0x1F0, ctrl 0x3F6, select 0xB0)
//   drive 2 = secondary master  (IO 0x170, ctrl 0x376, select 0xA0)
//   drive 3 = secondary slave   (IO 0x170, ctrl 0x376, select 0xB0)
//
// ata_probe_all() определяет, кто реально подключён (ATA IDENTIFY).
#include "ata.h"
#include "io.h"

typedef struct {
    unsigned short io;      // база данных-портов (0x1F0 / 0x170)
    unsigned short ctrl;    // control/alt-status (0x3F6 / 0x376)
    unsigned char  sel;     // drive select бит (0xA0 master, 0xB0 slave)
} ata_ch_t;

static const ata_ch_t CH[ATA_MAX_DRIVES] = {
    { 0x1F0, 0x3F6, 0xE0 },
    { 0x1F0, 0x3F6, 0xF0 },
    { 0x170, 0x376, 0xE0 },
    { 0x170, 0x376, 0xF0 },
};

static int g_drive = 0;
static int g_present[ATA_MAX_DRIVES] = {0, 0, 0, 0};

void ata_select(int drive) {
    if (drive < 0) drive = 0;
    if (drive >= ATA_MAX_DRIVES) drive = ATA_MAX_DRIVES - 1;
    g_drive = drive;
}
int ata_current(void) { return g_drive; }
int ata_present(int drive) {
    if (drive < 0 || drive >= ATA_MAX_DRIVES) return 0;
    return g_present[drive];
}

// 400ns задержка: 4 чтения alt-status.
static void ata_delay400(const ata_ch_t* c) {
    inb(c->ctrl); inb(c->ctrl); inb(c->ctrl); inb(c->ctrl);
}

// IDENTIFY для устройства. Возвращает 1, если устройство есть и ATA.
static int ata_identify(const ata_ch_t* c) {
    // Выбрать устройство.
    outb(c->io + 6, c->sel);
    ata_delay400(c);
    // Обнулить секторный счётчик/регистры.
    outb(c->io + 2, 0);
    outb(c->io + 3, 0);
    outb(c->io + 4, 0);
    outb(c->io + 5, 0);
    // Команда IDENTIFY (0xEC).
    outb(c->io + 7, 0xEC);
    ata_delay400(c);
    unsigned char st = inb(c->io + 7);
    if (st == 0) return 0;              // нет устройства
    // Ждём сброса BSY, затем проверяем DRQ/ERR.
    for (unsigned int t = 0; t < 1000000u; t++) {
        st = inb(c->io + 7);
        if (!(st & 0x80)) break;         // BSY сброшен
    }
    if (st & 0x01) return 0;             // ERR — не ATA
    if (!(st & 0x08)) return 0;          // нет DRQ
    // Прочитать 256 слов (иначе устройство зависнет).
    for (int i = 0; i < 256; i++) inw(c->io);
    return 1;
}

void ata_probe_all(void) {
    for (int d = 0; d < ATA_MAX_DRIVES; d++) {
        g_present[d] = ata_identify(&CH[d]);
    }
}

void ata_read_sector(unsigned int lba, unsigned char* buffer) {
    const ata_ch_t* c = &CH[g_drive];
    for (unsigned int w = 0; inb(c->io + 7) & 0x80; w++) if (w > 1000000u) return;

    outb(c->io + 6, c->sel | ((lba >> 24) & 0x0F));
    ata_delay400(c);
    outb(c->io + 2, 1);
    outb(c->io + 3, (unsigned char)lba);
    outb(c->io + 4, (unsigned char)(lba >> 8));
    outb(c->io + 5, (unsigned char)(lba >> 16));
    outb(c->io + 7, 0x20);

    for (unsigned int t = 0; ; t++) {
        unsigned char st = inb(c->io + 7);
        if (!(st & 0x80) && (st & 0x08)) break;
        if (st & 0x01) return;
        if (t >= 1000000u) return;
    }
    for (int i = 0; i < 256; i++) {
        unsigned short d = inw(c->io);
        buffer[i * 2] = d & 0xFF;
        buffer[i * 2 + 1] = (d >> 8) & 0xFF;
    }
}

void ata_write_sector(unsigned int lba, const unsigned char* buffer) {
    const ata_ch_t* c = &CH[g_drive];
    for (unsigned int w = 0; inb(c->io + 7) & 0x80; w++) if (w > 1000000u) return;

    outb(c->io + 6, c->sel | ((lba >> 24) & 0x0F));
    ata_delay400(c);
    outb(c->io + 2, 1);
    outb(c->io + 3, (unsigned char)lba);
    outb(c->io + 4, (unsigned char)(lba >> 8));
    outb(c->io + 5, (unsigned char)(lba >> 16));
    outb(c->io + 7, 0x30);

    for (unsigned int t = 0; ; t++) {
        unsigned char st = inb(c->io + 7);
        if (!(st & 0x80) && (st & 0x08)) break;
        if (st & 0x01) return;
        if (t >= 1000000u) return;
    }
    for (int i = 0; i < 256; i++) {
        outw(c->io, buffer[i * 2] | (buffer[i * 2 + 1] << 8));
    }
    for (unsigned int t = 0; ; t++) {
        if (!(inb(c->io + 7) & 0x80)) break;
        if (t >= 1000000u) return;
    }
}
