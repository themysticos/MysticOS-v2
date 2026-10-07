#!/usr/bin/env python3
import sys, struct
img, fname = sys.argv[1], sys.argv[2]
d = open(img,'rb').read()
bps = struct.unpack_from('<H', d, 11)[0]
spc = d[13]
resv = struct.unpack_from('<H', d, 14)[0]
nfats = d[16]
spf = struct.unpack_from('<H', d, 22)[0]
root_ents = struct.unpack_from('<H', d, 17)[0]
fat_start = resv*bps
root_start = (resv + nfats*spf)*bps
name = fname.upper()
def fat_next(c):
    return struct.unpack_from('<H', d, fat_start + c*2)[0]
cluster=None; size=None
for e in range(root_ents):
    o = root_start + e*32
    nm = d[o:o+11]
    if nm[0]==0: break
    if nm[0]==0xE5: continue
    s = nm[:8].decode('latin1').rstrip()
    ext = nm[8:11].decode('latin1').rstrip()
    full = (s+'.'+ext) if ext else s
    if full.upper()==name:
        cluster = struct.unpack_from('<H', d, o+26)[0]
        size = struct.unpack_from('<I', d, o+28)[0]
        print('found', full, 'start', cluster, 'size', size)
        break
if cluster is None:
    print('NOT FOUND'); sys.exit(1)
need = (size + spc*bps - 1)//(spc*bps)
print('clusters needed', need, 'spc', spc)
c=cluster; cnt=0; seen=set()
while c>=2 and c<0xFFF8:
    if c in seen: print('LOOP'); break
    seen.add(c); cnt+=1
    c = fat_next(c)
print('chain length', cnt, 'last', hex(c))
