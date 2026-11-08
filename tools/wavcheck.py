import struct

d = open('MEG.WAV','rb').read()
i = 12
data = b''
while i < len(d):
    cid = d[i:i+4]
    sz = struct.unpack('<I', d[i+4:i+8])[0]
    if cid == b'data':
        data = d[i+8:i+8+sz]
        break
    i += 8 + sz + (sz & 1)

n = len(data) // 2
v = struct.unpack('<%dh' % n, data[:n*2])
print('samples', n)
print('peak+', max(v), 'peak-', min(v))
print('clip', sum(1 for x in v if x >= 32767 or x <= -32768))
mj = 0
for a, b in zip(v, v[1:]):
    j = abs(b - a)
    if j > mj: mj = j
print('maxjump', mj)
