#!/usr/bin/env python3
p = 'i_oplmusic.c'
s = open(p, 'rb').read().decode('latin1')
marker = '#include "midifile.h"\r\n'
shim = ('#include "midifile.h"\r\n'
        '\r\n'
        '/* MysticOS shims (absent in our libc/doomgeneric) */\r\n'
        'static char* getenv(const char* n) { (void)n; return 0; }\r\n'
        '#ifndef M_remove\r\n'
        '#define M_remove remove\r\n'
        '#endif\r\n')
assert marker in s, 'marker not found'
if 'MysticOS shims' not in s:
    s = s.replace(marker, shim, 1)
# encode FIRST, then write (безопасно при ошибке кодировки)
data = s.encode('latin1')
open(p, 'wb').write(data)
print('patched, size', len(data))
