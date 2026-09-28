#!/usr/bin/env python3
# pnginfo.py - сколько уникальных цветов в PNG.
import sys, importlib.util
from collections import Counter
spec = importlib.util.spec_from_file_location('p', 'tools/png2icon.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
for path in sys.argv[1:]:
    w, h, px = m.read_png(path)
    c = Counter(px)
    print(path, w, 'x', h, 'unique:', len(c))
    for col, n in c.most_common(5):
        print('   ', col, n)
