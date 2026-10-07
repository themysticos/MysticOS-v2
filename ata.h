// ata.h - драйвер ATA PIO (LBA28), до 4 устройств.
//
// Раскладка drive:
//   0 = primary master,   1 = primary slave
//   2 = secondary master, 3 = secondary slave
//
// ata_select(d) выбирает устройство для последующих read/write.
// ata_present(d) — есть ли реально устройство (определяется ata_probe_all()).
#ifndef ATA_H
#define ATA_H

#define ATA_MAX_DRIVES 4

void ata_select(int drive);
int  ata_current(void);

// Определить все 4 устройства (кто присутствует). Звать один раз при старте.
void ata_probe_all(void);

// Есть ли устройство drive (0..3). 1/0.
int  ata_present(int drive);

void ata_read_sector(unsigned int lba, unsigned char* buffer);
void ata_write_sector(unsigned int lba, const unsigned char* buffer);

#endif
