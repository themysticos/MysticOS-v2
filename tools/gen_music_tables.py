#!/usr/bin/env python3
# Генерит music_tables.h: частоты 128 MIDI-нот + синус-таблица 1024.
import math
lines = []
lines.append('// music_tables.h - автогенерация. Не править руками.')
lines.append('#ifndef MUSIC_TABLES_H')
lines.append('#define MUSIC_TABLES_H')
lines.append('')
lines.append('// Частоты MIDI-нот 0..127 (Гц), округлены.')
lines.append('static const unsigned short g_note_freq[128] = {')
row = []
for n in range(128):
    f = 440.0 * (2.0 ** ((n - 69) / 12.0))
    row.append(str(int(round(f))))
    if len(row) == 12:
        lines.append('    ' + ', '.join(row) + ',')
        row = []
if row:
    lines.append('    ' + ', '.join(row) + ',')
lines.append('};')
lines.append('')
lines.append('// Синус: 1024 отсчёта, int16. Большая таблица + интерполяция = меньше алиасинга.')
lines.append('static const short g_sine[2048] = {')
row = []
for i in range(2048):
    v = int(round(32767.0 * math.sin(2.0 * math.pi * i / 2048.0)))
    row.append(str(v))
    if len(row) == 12:
        lines.append('    ' + ', '.join(row) + ',')
        row = []
if row:
    lines.append('    ' + ', '.join(row) + ',')
lines.append('};')
lines.append('')
lines.append('#endif // MUSIC_TABLES_H')
open('music_tables.h', 'w').write('\n'.join(lines) + '\n')
print('written music_tables.h (1024 sine)')
