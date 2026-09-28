#!/usr/bin/env python3
import struct, sys
path = sys.argv[1] if len(sys.argv) > 1 else 'disk.img'
d = open(path, 'rb').read(64)
print('bps      ', struct.unpack_from('<H', d, 11)[0])
print('spc      ', d[13])
print('reserved ', struct.unpack_from('<H', d, 14)[0])
print('nfats    ', d[16])
print('root_ents', struct.unpack_from('<H', d, 17)[0])
print('total16  ', struct.unpack_from('<H', d, 19)[0])
print('spf      ', struct.unpack_from('<H', d, 22)[0])
print('total32  ', struct.unpack_from('<I', d, 32)[0])
