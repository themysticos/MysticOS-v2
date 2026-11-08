#!/usr/bin/env python3
# msfs2_dump.py - отладка: вывести суперблок и блоки образа MSFS2.
import sys, struct

if len(sys.argv) < 2:
    print('usage: msfs2_dump.py <image>')
    sys.exit(1)

img = open(sys.argv[1], 'rb').read()
magic = img[0:4]
ver = struct.unpack_from('<H', img, 4)[0]
bs = struct.unpack_from('<I', img, 6)[0]
total = struct.unpack_from('<I', img, 10)[0]
root = struct.unpack_from('<I', img, 14)[0]
print(f'SUPERBLOCK: magic={magic} ver={ver} block_size={bs} total_blocks={total} root_lba={root}')

for i in range(1, total):
    b = img[i*bs:(i+1)*bs]
    if len(b) < bs:
        break
    if b[0:4] != b'MSFS':
        continue
    typ = b[4]
    nlen = b[5]
    if typ == 0x01:
        datalen = struct.unpack_from('<I', b, 6)[0]
        name = b[10:10+nlen].decode('utf-8', 'replace')
        # найти END-маркер после данных
        end_off = 10 + nlen + datalen
        etype = b[end_off+4] if end_off+4 < bs else -1
        nxt = struct.unpack_from('<I', b, end_off+6)[0] if end_off+6+4 <= bs else -1
        print(f'  block {i}: START name="{name}" datalen={datalen} next_lba={nxt}')
