#!/usr/bin/env python3
# lsimg2.py - рекурсивный список файлов FAT16-образа MysticOS.
import struct, sys

SECTOR = 512
ATTR_DIR = 0x10

img = open(sys.argv[1] if len(sys.argv) > 1 else 'mysticos2.img', 'rb').read()
res = struct.unpack_from('<H', img, 14)[0]
nf = img[16]
re_ = struct.unpack_from('<H', img, 17)[0]
spf = struct.unpack_from('<H', img, 22)[0]
spc = img[13]
root = res + nf * spf
root_sectors = (re_ * 32 + SECTOR - 1) // SECTOR
data = root + root_sectors
cluster_size = spc * SECTOR


def fat_get(cl):
    return struct.unpack_from('<H', img, res * SECTOR + cl * 2)[0]


def dir_sectors(dir_cluster):
    if dir_cluster == 0:
        return root, root + root_sectors
    s = data + (dir_cluster - 2) * spc
    return s, s + spc


def list_dir(dir_cluster, prefix):
    s, e = dir_sectors(dir_cluster)
    for sec in range(s, e):
        base = sec * SECTOR
        for ent in range(SECTOR // 32):
            off = base + ent * 32
            rec = img[off:off + 32]
            if rec[0] == 0:
                return
            if rec[0] == 0xE5 or (rec[11] & 0x0F) == 0x0F:
                continue
            nm = rec[0:8].decode('ascii', 'ignore').rstrip()
            ext = rec[8:11].decode('ascii', 'ignore').rstrip()
            name = nm + ('.' + ext if ext else '')
            size = struct.unpack_from('<I', rec, 28)[0]
            fc = struct.unpack_from('<H', rec, 26)[0] | (struct.unpack_from('<H', rec, 20)[0] << 16)
            if rec[11] & ATTR_DIR:
                if nm == '.' or nm == '..':
                    continue
                print(f'  {prefix}{name}/  (dir, cl={fc})')
                list_dir(fc, prefix + name + '/')
            else:
                print(f'  {prefix}{name}  {size} bytes  (cl={fc})')


list_dir(0, '')
