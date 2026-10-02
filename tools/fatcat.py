#!/usr/bin/env python3
# fatcat.py - извлечь файл из FAT16-образа MysticOS.
#
# Использование:
#   python3 tools/fatcat.py <image> <path>              # вывод в stdout
#   python3 tools/fatcat.py <image> <path> -o <outfile> # вывод в файл
#
# Путь — как в lsimg2.py: 'README.TXT', 'KERNEL/SYSPR/TERM.ELF'.
# Регистр — не важен (сравниваем в верхнем регистре).
import struct, sys

SECTOR = 512
ATTR_DIR = 0x10

def main():
    if len(sys.argv) < 3:
        print(__doc__ or 'usage: fatcat.py <image> <path> [-o out]')
        sys.exit(1)
    img_path = sys.argv[1]
    target = sys.argv[2].strip('/').upper()
    out_path = None
    if len(sys.argv) >= 5 and sys.argv[3] == '-o':
        out_path = sys.argv[4]

    img = open(img_path, 'rb').read()
    res = struct.unpack_from('<H', img, 14)[0]
    nf = img[16]
    re_ = struct.unpack_from('<H', img, 17)[0]
    spf = struct.unpack_from('<H', img, 22)[0]
    spc = img[13]
    root = res + nf * spf
    root_sectors = (re_ * 32 + SECTOR - 1) // SECTOR
    data = root + root_sectors

    def fat_get(cl):
        return struct.unpack_from('<H', img, res * SECTOR + cl * 2)[0]

    def dir_sectors(dc):
        if dc == 0:
            return root, root + root_sectors
        s = data + (dc - 2) * spc
        return s, s + spc

    def read_chain(first_cluster, size):
        out = b''
        cl = first_cluster
        while len(out) < size and cl not in (0, 0xFFFF, 0xFFF8, 0xFFF9, 0xFFFA, 0xFFFB, 0xFFFC, 0xFFFD, 0xFFFE):
            lba = data + (cl - 2) * spc
            out += img[lba * SECTOR: (lba + spc) * SECTOR]
            cl = fat_get(cl)
        return out[:size]

    def find_in_dir(dir_cluster, name_upper):
        s, e = dir_sectors(dir_cluster)
        for sec in range(s, e):
            base = sec * SECTOR
            for ent in range(SECTOR // 32):
                off = base + ent * 32
                rec = img[off:off + 32]
                if rec[0] == 0:
                    return None
                if rec[0] == 0xE5 or (rec[11] & 0x0F) == 0x0F:
                    continue
                nm = rec[0:8].decode('ascii', 'ignore').rstrip()
                ext = rec[8:11].decode('ascii', 'ignore').rstrip()
                full = (nm + ('.' + ext if ext else '')).upper()
                if full == name_upper:
                    size = struct.unpack_from('<I', rec, 28)[0]
                    fc = struct.unpack_from('<H', rec, 26)[0] | (struct.unpack_from('<H', rec, 20)[0] << 16)
                    is_dir = bool(rec[11] & ATTR_DIR)
                    return fc, size, is_dir
        return None

    cur_dir = 0
    parts = target.split('/')
    for i, part in enumerate(parts):
        hit = find_in_dir(cur_dir, part)
        if hit is None:
            print(f'[fatcat] not found: {part}', file=sys.stderr)
            sys.exit(2)
        fc, size, is_dir = hit
        if i == len(parts) - 1:
            if is_dir:
                print(f'[fatcat] is a directory: {target}', file=sys.stderr)
                sys.exit(3)
            content = read_chain(fc, size)
            if out_path:
                open(out_path, 'wb').write(content)
                print(f'[fatcat] wrote {len(content)} bytes -> {out_path}')
            else:
                sys.stdout.buffer.write(content)
            return
        if not is_dir:
            print(f'[fatcat] not a directory: {part}', file=sys.stderr)
            sys.exit(4)
        cur_dir = fc

if __name__ == '__main__':
    main()
