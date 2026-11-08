#!/usr/bin/env python3
# gen_dcn10_regs.py - извлечь оффсеты регистров DCN 1.0 из Linux-заголовка
# и сгенерировать C-заголовок dcn10_regs.h для нашего драйвера.
#
# Использование:
#   python3 tools/gen_dcn10_regs.py <dcn_1_0_offset.h> > dcn10_regs.h
import sys, re

# Префиксы нужных регистров (pipe 0 + общие).
WANT_PREFIXES = [
    "HUBP0_", "HUBP1_",           # память/запросы
    "HUBPREQ0_", "HUBPREQ1_",     # запросчик (адрес framebuffer!)
    "DPP0_", "DPP1_",             # форматирование пикселей
    "OPP0_", "OPP1_",             # output
    "MPC0_", "MPC1_",             # композиция
    "OTG0_", "OTG1_",             # timing generator (OPTС)
    "DIO_", "DP_", "DIG", "DIG0_",   # вывод/encoder
    "CNVC_", "CNVC_CFG0_", "CNVC_CUR0_",  # DPP: format converter
    "DSCL0_", "FMT_", "DPG0_", "CURSOR0_",  # scaler, format, debug
    "DCCG_", "DCCG0_",           # клоки
    "HUBBUB_", "DCHUBBUB",       # hubbub
    "DC_"                         # общие
]

def want(name):
    for p in WANT_PREFIXES:
        if name.startswith(p):
            return True
    return False

def main():
    if len(sys.argv) < 2:
        print("usage: gen_dcn10_regs.py <dcn_1_0_offset.h>", file=sys.stderr)
        return 1
    seen = set()
    out = []
    out.append("// AUTO-GENERATED from dcn_1_0_offset.h (DCN 1.0, Vega). Не править.\n")
    out.append("#ifndef DCN10_REGS_H\n#define DCN10_REGS_H\n\n")
    rx = re.compile(r'^#define\s+mm(\S+)\s+0x([0-9a-fA-F]+)\s*$')
    with open(sys.argv[1], 'r', errors='replace') as f:
        for line in f:
            m = rx.match(line.strip())
            if not m: continue
            name, val = m.group(1), m.group(2)
            if name.endswith("_BASE_IDX"): continue
            if not want(name): continue
            if name in seen: continue
            seen.add(name)
            out.append(f"#define mm{name} 0x{val}u\n")
    out.append("\n#endif // DCN10_REGS_H\n")
    sys.stdout.write("".join(out))
    print(f"// извлечено {len(seen)} регистров", file=sys.stderr)
    return 0

if __name__ == '__main__':
    sys.exit(main())
