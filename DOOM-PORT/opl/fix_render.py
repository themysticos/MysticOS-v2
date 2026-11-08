#!/usr/bin/env python3
p = 'opl_mysticos.c'
s = open(p, 'rb').read().decode('latin1')
marker = 'void opl_mysticos_render(short* out, unsigned int nsamples) {'
i = s.find(marker)
assert i >= 0, 'render not found'
# конец функции — первая '}' на отдельной строке после i
j = s.find('\n}\n', i)
assert j >= 0
j += len('\n}\n')
new = (
'/* Generate while invoking i_oplmusic callbacks at exact moments\n'
'   (small chunks). Otherwise notes fire in a batch at buffer end -> choppy. */\n'
'void opl_mysticos_render(short* out, unsigned int nsamples) {\n'
'    if (!g_inited) {\n'
'        unsigned int k;\n'
'        for (k = 0; k < nsamples * 2; k++) out[k] = 0;\n'
'        return;\n'
'    }\n'
'    unsigned int done = 0;\n'
'    while (done < nsamples) {\n'
'        unsigned int chunk = nsamples - done;\n'
'        if (chunk > 64) chunk = 64;   /* ~1.3 ms: точность включения нот */\n'
'        OPL3_GenerateStream(&g_chip, out + (unsigned long)done * 2, chunk);\n'
'        done += chunk;\n'
'        if (g_paused) continue;\n'
'        g_us += (uint64_t)(((unsigned long)chunk * 1000000ul) / g_rate);\n'
'        int m;\n'
'        for (m = 0; m < OPL_MAX_CALLBACKS; m++) {\n'
'            if (g_cbs[m].used && g_us >= g_cbs[m].time) {\n'
'                opl_callback_t cb = g_cbs[m].cb;\n'
'                void *data = g_cbs[m].data;\n'
'                g_cbs[m].used = 0;\n'
'                if (cb && !g_lock) cb(data);\n'
'            }\n'
'        }\n'
'    }\n'
'}\n')
s = s[:i] + new + s[j:]
open(p, 'wb').write(s.encode('latin1'))
print('patched')
