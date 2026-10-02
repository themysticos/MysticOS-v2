# mbr.py - вписать таблицу разделов MBR в образ (для BIOS-USB boot).
#
# Зачем: BIOS видит USB-флешку ЗАГРУЗОЧНОЙ только если в секторе 0 есть
# валидная таблица разделов с активным (bootable) разделом. mkfs.fat и
# наш загрузчик её НЕ создают — таблица пустая, BIOS игнорит носитель.
#
# Использование:
#   python3 mbr.py IMAGE [start_lba] [num_sectors]
# По умолчанию: start_lba=1, num_sectors = (размер образа в секторах) - 1.
#
# Формат записи (16 байт на offset 446 = 0x1BE):
#   [0]    0x80        активный (bootable)
#   [1-3]  CHS начала  (0xFE,0xFF,0xFF — заглушка; BIOS берёт LBA)
#   [4]    0x0E        тип: FAT16 LBA
#   [5-7]  CHS конца   (заглушка)
#   [8-11] LBA start   (little-endian)
#   [12-15] число секторов (little-endian)

import sys
import struct

PART_OFF = 446          # 0x1BE
PART_TYPE_FAT16_LBA = 0x0E


def main():
    if len(sys.argv) < 2:
        print("usage: python3 mbr.py IMAGE [start_lba] [num_sectors]")
        sys.exit(1)
    img = sys.argv[1]
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    with open(img, "r+b") as f:
        f.seek(0, 2)
        total = f.tell()
        total_sectors = total // 512
        if len(sys.argv) > 3:
            num = int(sys.argv[3])
        else:
            num = total_sectors - start

        # Запись раздела: active, CHS-заглушки, type, LBA start, num sectors.
        entry = struct.pack(
            "<B3sB3sII",
            0x80,                       # active / bootable
            bytes([0xFE, 0xFF, 0xFF]),  # CHS begin (заглушка)
            PART_TYPE_FAT16_LBA,        # type = FAT16 LBA
            bytes([0xFE, 0xFF, 0xFF]),  # CHS end (заглушка)
            start,                      # LBA start
            num,                        # число секторов
        )
        assert len(entry) == 16
        f.seek(PART_OFF)
        f.write(entry)
        # Остальные 3 записи оставляем нулевыми (уже нули в образе).

    print(f"MBR: partition active, type 0x0E, start LBA={start}, "
          f"sectors={num} ({num // 2} KiB)")


if __name__ == "__main__":
    main()
