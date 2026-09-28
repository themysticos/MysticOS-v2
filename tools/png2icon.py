#!/usr/bin/env python3
# png2icon.py - конвертирует PNG (RGBA) в C-массив ARGB. Без внешних зависимостей.
# Использование: python3 tools/png2icon.py icon.png ICON_NAME
import sys, zlib, struct

def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos = 8
    w = h = bitd = ctype = 0
    idat = b''
    while pos < len(data):
        ln = struct.unpack('>I', data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+ln]
        pos += 12 + ln
        if typ == b'IHDR':
            w, h, bitd, ctype = struct.unpack('>IIBB', chunk[:10])
        elif typ == b'IDAT':
            idat += chunk
        elif typ == b'IEND':
            break
    raw = zlib.decompress(idat)
    # Определяем число каналов
    ch = {0:1, 2:3, 3:1, 4:2, 6:4}[ctype]
    bpp = ch  # 8-bit на канал
    stride = w * bpp
    # Разфильтровываем
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        ft = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        if ft == 1:   # Sub
            for x in range(bpp, stride): line[x] = (line[x] + line[x-bpp]) & 0xFF
        elif ft == 2: # Up
            for x in range(stride): line[x] = (line[x] + prev[x]) & 0xFF
        elif ft == 3: # Average
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ft == 4: # Paeth
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x-bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        out += line
        prev = line
    # Преобразуем в RGBA
    px = []
    for y in range(h):
        for x in range(w):
            o = (y*w + x) * bpp
            if ctype == 6:   r,g,b,a = out[o],out[o+1],out[o+2],out[o+3]
            elif ctype == 2: r,g,b,a = out[o],out[o+1],out[o+2],255
            elif ctype == 0: r=g=b=out[o]; a=255
            else: r=g=b=0; a=255
            px.append((r,g,b,a))
    return w, h, px

def main():
    path, name = sys.argv[1], sys.argv[2]
    w, h, px = read_png(path)
    print(f'// Generated from {path} ({w}x{h})')
    print(f'#include "icons.h"')
    print(f'const unsigned int {name}_pixels[{w*h}] = {{')
    for y in range(h):
        row = []
        for x in range(w):
            r,g,b,a = px[y*w+x]
            val = 0 if a < 128 else (0xFF000000 | (r<<16) | (g<<8) | b)
            row.append(f'0x{val:08X}')
        print('    ' + ','.join(row) + ',')
    print('};')

if __name__ == '__main__':
    main()
