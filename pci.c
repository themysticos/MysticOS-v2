// pci.c - сканер PCI-шины (legacy config space 0xCF8/0xCFC).
#include "pci.h"
#include "io.h"
#include "console.h"

// ============================================================
// Config space через IO-порты (legacy)
// ============================================================
// 0xCF8: 32-битный адрес. Биты:
//   [31]    enable (1 = обращение к config space)
//   [23:16] bus
//   [15:11] slot
//   [10:8]  func
//   [7:2]   offset (dword-aligned)
// 0xCFC: 32-битные данные.
//
// ВАЖНО: legacy config IO работает ТОЛЬКО если в системе нет
// другого механизма (PCIe ECAM через MMIO). В QEMU и большинстве
// BIOS-загруженных систем это работает.

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

static unsigned int pci_make_addr(int bus, int slot, int func, int offset) {
    return 0x80000000u
         | ((unsigned int)(bus  & 0xFF) << 16)
         | ((unsigned int)(slot & 0x1F) << 11)
         | ((unsigned int)(func & 0x07) << 8)
         | ((unsigned int)(offset & 0xFC));
}

unsigned int pci_read32(int bus, int slot, int func, int offset) {
    unsigned int addr = pci_make_addr(bus, slot, func, offset);
    __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((unsigned short)PCI_CONFIG_ADDR));
    unsigned int val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"((unsigned short)PCI_CONFIG_DATA));
    return val;
}

void pci_write32(int bus, int slot, int func, int offset, unsigned int value) {
    unsigned int addr = pci_make_addr(bus, slot, func, offset);
    __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((unsigned short)PCI_CONFIG_ADDR));
    __asm__ volatile ("outl %0, %1" :: "a"(value), "Nd"((unsigned short)PCI_CONFIG_DATA));
}

unsigned short pci_read16(int bus, int slot, int func, int offset) {
    unsigned int dword = pci_read32(bus, slot, func, offset & ~3);
    int shift = (offset & 2) * 8;
    return (unsigned short)((dword >> shift) & 0xFFFF);
}

unsigned char pci_read8(int bus, int slot, int func, int offset) {
    unsigned int dword = pci_read32(bus, slot, func, offset & ~3);
    int shift = (offset & 3) * 8;
    return (unsigned char)((dword >> shift) & 0xFF);
}

// ============================================================
// BAR info
// ============================================================
int pci_bar_info(int bus, int slot, int func, int bar_idx,
                 unsigned int* addr_lo, unsigned int* addr_hi,
                 unsigned int* size, int* is_io, int* is_64) {
    int offset = 0x10 + bar_idx * 4;
    unsigned int orig = pci_read32(bus, slot, func, offset);
    if (orig == 0) return 0;

    int io = (orig & 0x1) ? 1 : 0;
    int type = (orig >> 1) & 0x3;
    int is64 = (type == 0x2);   // MMIO64

    pci_write32(bus, slot, func, offset, 0xFFFFFFFFu);
    unsigned int mask = pci_read32(bus, slot, func, offset);
    pci_write32(bus, slot, func, offset, orig);

    if (io) {
        unsigned int sz = ~(mask & 0xFFFFFFFCu) + 1u;
        *size = sz;
        *is_io = 1;
        *is_64 = 0;
        *addr_lo = orig & 0xFFFFFFFCu;
        *addr_hi = 0;
        return 1;
    }

    unsigned int sz = ~(mask & 0xFFFFFFF0u) + 1u;
    *size = sz;
    *is_io = 0;
    *is_64 = is64;
    *addr_lo = orig & 0xFFFFFFF0u;
    *addr_hi = 0;

    if (is64) {
        unsigned int hi = pci_read32(bus, slot, func, offset + 4);
        *addr_hi = hi;
    }
    return 1;
}

// ============================================================
// IRQ
// ============================================================
int pci_irq_line(int bus, int slot, int func) {
    unsigned char line = pci_read8(bus, slot, func, 0x3C);
    if (line == 0 || line == 0xFF) return -1;
    return line;
}

// ============================================================
// Скан
// ============================================================
void pci_enumerate(pci_enum_cb_t cb) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                unsigned int cfg0 = pci_read32(bus, slot, func, 0);
                unsigned int vendor = cfg0 & 0xFFFF;
                if (vendor == 0xFFFF || vendor == 0x0000) {
                    if (func == 0) break;
                    continue;
                }
                unsigned int device = (cfg0 >> 16) & 0xFFFF;
                unsigned int class_rev = pci_read32(bus, slot, func, 8);
                unsigned int cls = (class_rev >> 16) & 0xFFFF;
                if (cb(bus, slot, func, vendor, device, cls) == 0) return;
            }
        }
    }
}

// ============================================================
// Отладка
// ============================================================
void pci_dump(void) {
    console_print("PCI devices:\n");
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                unsigned int cfg0 = pci_read32(bus, slot, func, 0);
                unsigned int vendor = cfg0 & 0xFFFF;
                if (vendor == 0xFFFF || vendor == 0x0000) {
                    if (func == 0) break;
                    continue;
                }
                unsigned int device = (cfg0 >> 16) & 0xFFFF;
                unsigned int cls = (pci_read32(bus, slot, func, 8) >> 16) & 0xFFFF;
                console_print("  ");
                console_print_hex(bus);
                console_print(":");
                console_print_hex(slot);
                console_print(".");
                console_print_hex(func);
                console_print(" v=");
                console_print_hex(vendor);
                console_print(" d=");
                console_print_hex(device);
                console_print(" c=");
                console_print_hex(cls);
                console_print("\n");
            }
        }
    }
    console_print("(end)\n");
}
