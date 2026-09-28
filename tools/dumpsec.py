#!/usr/bin/env python3
# dumpsec.py - показать записи каталога FAT по сектору.
import sys
path = sys.argv[1]
sec = int(sys.argv[2])
d = open(path, 'rb').read()
off = sec * 512
print(f'--- sector {sec} (offset {off}) ---')
for e in range(8):
    rec = d[off + e*32: off + e*32 + 32]
    if len(rec) < 32: break
    name = rec[0:11]
    print(f'  entry {e}: name={name} attr={rec[11]:#x} first_cl={rec[26]|(rec[27]<<8)}')
