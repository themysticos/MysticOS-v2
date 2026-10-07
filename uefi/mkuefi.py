#!/usr/bin/env python3
# mkuefi.py - собирает UEFI-загрузочный образ: MBR + 1 раздел EFI System (0xEF)
# + FAT32 внутри + EFI/BOOT/BOOTX64.EFI + kernel.elf.
#
# Зачем: OVMF (UEFI) НЕ грузит "superfloppy" (FAT прямо с сектора 0).
# Нужна таблица разделов MBR/GPT с ESP. BIOS это терпел, UEFI - нет.
#
# Как: FAT32 собирается в ОТДЕЛЬНОМ файле (mcopy понимает только так),
# затем вставляется в образ dd по смещению раздела. Без root.
#
# Использование:
#   python3 mkuefi.py out.img BOOTX64.EFI kernel.elf
import os, sys, struct, subprocess, tempfile

SECTOR = 512
START_LBA = 2048          # 1 MiB - стандартное выравнивание ESP
PART_SECTORS = 2048 * 64  # 64 MiB под ESP (FAT32 требует >= ~33 MB)

def run(cmd):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"command failed: {cmd}")
    return r.stdout

def build_mbr(part_lba, part_sectors):
    mbr = bytearray(SECTOR)
    off = 446
    mbr[off+0] = 0x80
    mbr[off+1] = 0x00
    mbr[off+2] = 0x02
    mbr[off+3] = 0x00
    mbr[off+4] = 0xEF                       # EFI System Partition
    mbr[off+5] = 0xFE
    mbr[off+6] = 0xFF
    mbr[off+7] = 0xFF
    struct.pack_into('<I', mbr, off+8,  part_lba)
    struct.pack_into('<I', mbr, off+12, part_sectors)
    mbr[510] = 0x55
    mbr[511] = 0xAA
    return bytes(mbr)

def main():
    if len(sys.argv) < 4:
        print("usage: mkuefi.py out.img BOOTX64.EFI kernel.elf")
        return 1
    out, bootx64, kernel = sys.argv[1], sys.argv[2], sys.argv[3]

    total_sectors = START_LBA + PART_SECTORS

    # 1. FAT32 в отдельном файле.
    with tempfile.NamedTemporaryFile(suffix='.fat', delete=False) as t:
        fat_path = t.name
    try:
        with open(fat_path, 'wb') as f:
            f.truncate(PART_SECTORS * SECTOR)
        run(f"mkfs.fat -F 32 {fat_path}")
        run(f"mmd -i {fat_path} ::/EFI")
        run(f"mmd -i {fat_path} ::/EFI/BOOT")
        run(f"mcopy -i {fat_path} {bootx64} ::/EFI/BOOT/BOOTX64.EFI")
        run(f"mcopy -i {fat_path} {kernel} ::/kernel.elf")

        # 2. Образ целиком.
        with open(out, 'wb') as f:
            f.truncate(total_sectors * SECTOR)

        # 3. MBR.
        with open(out, 'r+b') as f:
            f.seek(0)
            f.write(build_mbr(START_LBA, PART_SECTORS))

        # 4. Вставить FAT в раздел.
        run(f"dd if={fat_path} of={out} bs=512 seek={START_LBA} conv=notrunc status=none")
    finally:
        os.unlink(fat_path)

    print(f"mkuefi: {out}: MBR + ESP(FAT32) @LBA{START_LBA}, {PART_SECTORS} sectors")
    return 0

if __name__ == '__main__':
    sys.exit(main())
