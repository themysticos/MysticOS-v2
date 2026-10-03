// pci.h - сканер PCI-шины (legacy config space 0xCF8/0xCFC).
//
// Нужен для VDS (driver.h) и для будущих драйверов (virtio-gpu,
// virtio-net, rtl8723de).
//
// Пока только legacy IO-порты: QEMU и большинство реального железа
// это поддерживают. MMIO config (PCIe ECAM) — потом.
#ifndef PCI_H
#define PCI_H

// Прочитать 32-битное слово из config space.
// bus: 0..255, slot: 0..31, func: 0..7, offset: 0..252 (выровнено на 4).
unsigned int pci_read32(int bus, int slot, int func, int offset);
unsigned short pci_read16(int bus, int slot, int func, int offset);
unsigned char  pci_read8 (int bus, int slot, int func, int offset);

void pci_write32(int bus, int slot, int func, int offset, unsigned int value);

// Стандартные поля (упрощённо).
static inline unsigned int pci_vendor(unsigned int cfg0)   { return cfg0 & 0xFFFF; }
static inline unsigned int pci_device_id(unsigned int cfg0) { return (cfg0 >> 16) & 0xFFFF; }

// Определить размер BAR (маска). bar_idx: 0..5. Возвращает 0,
// если BAR не используется (0). 1 — IO, 2 — MMIO32, 3 — MMIO64.
// addr_lo/addr_hi — базовые адреса (hi = 0 для 32-bit).
// size — размер региона (степень двойки).
int pci_bar_info(int bus, int slot, int func, int bar_idx,
                 unsigned int* addr_lo, unsigned int* addr_hi,
                 unsigned int* size, int* is_io, int* is_64);

// Прочитать IRQ line (offset 0x3C) и IRQ pin (0x3D). -1 если не назначен.
int pci_irq_line(int bus, int slot, int func);

// Позвонить callback для каждого найденного устройства (vendor != 0xFFFF
// и != 0x0000). cb возвращает 1 — продолжать, 0 — остановить скан.
typedef int (*pci_enum_cb_t)(int bus, int slot, int func,
                             unsigned int vendor, unsigned int device,
                             unsigned int class_code);
void pci_enumerate(pci_enum_cb_t cb);

// Для отладки: печатать все устройства через console_print.
void pci_dump(void);

#endif // PCI_H
