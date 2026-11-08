import struct
for fn in ['SINE.WAV','MEG.WAV']:
    d = open(fn,'rb').read()
    i = 12
    while i < len(d):
        cid = d[i:i+4]
        sz = struct.unpack('<I', d[i+4:i+8])[0]
        if cid == b'fmt ':
            fmt, ch, rate, br, ba, bits = struct.unpack('<HHIIHH', d[i+8:i+8+16])
            print(fn, 'fmt', fmt, 'ch', ch, 'rate', rate, 'bits', bits, 'block', ba)
            break
        i += 8 + sz + (sz & 1)
