#!/usr/bin/env python3
# lsimg.py - список файлов в корне FAT16-образа MysticOS.
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'mysticos.img'
img = open(path, 'rb').read()
bps = struct.unpack_from('<H', img, 11)[0]
nf = img[16]
re_ = struct.unpack_from('<H', img, 17)[0]
spf = struct.unpack_from('<H', img, 22)[0]
res = struct.unpack_from('<H', img, 14)[0]

root = res + nf * spf
n = 0
for e in range(re_):
    off = root * bps + e * 32
    rec = img[off:off + 32]
    if rec[0] == 0:
        break
    if rec[0] == 0xE5:
        continue
    if (rec[11] & 0x0F) == 0x0F:
        continue
    name = rec[0:8].decode('ascii', 'ignore').rstrip()
    ext = rec[8:11].decode('ascii', 'ignore').rstrip()
    size = struct.unpack_from('<I', rec, 28)[0]
    print(f'  {name}.{ext}  {size} bytes')
    n += 1
print(f'total: {n} files')
