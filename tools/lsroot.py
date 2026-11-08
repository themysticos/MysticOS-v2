#!/usr/bin/env python3
import struct, sys
img = sys.argv[1] if len(sys.argv) > 1 else 'mysticos2.img'
d = open(img, 'rb').read()
bps = struct.unpack_from('<H', d, 11)[0]
nf = d[16]
spf = struct.unpack_from('<H', d, 22)[0]
res = struct.unpack_from('<H', d, 14)[0]
root = struct.unpack_from('<H', d, 17)[0]
root_sec = ((root * 32) + (bps - 1)) // bps
root_lba = res + nf * spf
off = root_lba * bps
for i in range(root):
    e = d[off + i*32: off + i*32 + 32]
    if e[0] == 0: break
    if e[0] == 0xE5 or (e[11] & 0x08): continue
    nm = e[0:8].decode('latin1').rstrip()
    ext = e[8:11].decode('latin1').rstrip()
    sz = struct.unpack_from('<I', e, 28)[0]
    print(f'  {nm}.{ext}  {sz}')
