// ata.h - драйвер ATA PIO (LBA28)
#ifndef ATA_H
#define ATA_H

void ata_select(int drive);
int  ata_current(void);
void ata_read_sector(unsigned int lba, unsigned char* buffer);
void ata_write_sector(unsigned int lba, const unsigned char* buffer);

#endif
