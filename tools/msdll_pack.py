#!/usr/bin/env python3
# msdll_pack.py - пост-линкер .msdll для MysticOS 2.
#
# Берёт ELF32 (собранный с dll/msdll.ld), читает символы _msdll_*_start/_end,
# собирает таблицу релокаций из .rel.* (R_386_32 -> ABS32), добавляет
# 64-байтный заголовок msdll_header_t в начало образа и пишет .msdll.
#
# Использование:
#   python3 tools/msdll_pack.py in.elf out.msdll
#
# Формат .msdll описан в dll/msdll_format.md.
import sys, struct

MSDLL_MAGIC   = 0x4C44534D
MSDLL_VERSION = 1
FLAG_RELOCS   = 0x1
FLAG_ENTRY    = 0x2

# ELF32
SHT_SYMTAB   = 2
SHT_REL      = 9
R_386_32     = 1     # абсолютный 32-бит (-> ABS32)

# --- минимальный ELF32-ридер ---
class Elf:
    def __init__(self, data):
        if data[:4] != b'\x7fELF':
            raise ValueError('not an ELF')
        if data[4] != 1:
            raise ValueError('not ELF32')
        self.data = data
        (self.e_entry, self.e_phoff, self.e_shoff,
         self.e_flags, self.e_ehsize, self.e_phentsize,
         self.e_phnum, self.e_shentsize, self.e_shnum,
         self.e_shstrndx) = struct.unpack_from('<IIIIHHHHHH', data, 24)
        self.sh = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            (name, typ, flags, addr, offset, size, link, info,
             align, entsize) = struct.unpack_from('<IIIIIIIIII', data, off)
            self.sh.append(dict(name=name, type=typ, addr=addr, offset=offset,
                                size=size, link=link, info=info, entsize=entsize))
        # Имена секций.
        self.shstr = b''
        if self.e_shstrndx < len(self.sh):
            s = self.sh[self.e_shstrndx]
            self.shstr = data[s['offset']:s['offset'] + s['size']]

    def sh_name(self, s):
        n = s['name']
        end = self.shstr.find(b'\0', n)
        return self.shstr[n:end].decode('latin1') if end >= 0 else ''

    def find_section(self, name):
        for s in self.sh:
            if self.sh_name(s) == name:
                return s
        return None

    def symbols(self):
        out = {}
        for s in self.sh:
            if s['type'] != SHT_SYMTAB:
                continue
            strtab = self.sh[s['link']]
            strs = self.data[strtab['offset']:strtab['offset'] + strtab['size']]
            n = s['size'] // s['entsize']
            for i in range(n):
                off = s['offset'] + i * s['entsize']
                st_name, st_value, st_size, st_info, st_other, st_shndx = \
                    struct.unpack_from('<IIIBBH', self.data, off)
                end = strs.find(b'\0', st_name)
                nm = strs[st_name:end].decode('latin1') if end >= 0 else ''
                if nm:
                    out[nm] = st_value
        return out

    def relocs(self):
        """Возвращает список (r_offset, r_type) из всех SHT_REL."""
        out = []
        for s in self.sh:
            if s['type'] != SHT_REL:
                continue
            n = s['size'] // 8
            for i in range(n):
                off = s['offset'] + i * 8
                r_offset, r_info = struct.unpack_from('<II', self.data, off)
                r_type = r_info & 0xFF
                out.append((r_offset, r_type))
        return out

def main():
    if len(sys.argv) != 3:
        print('usage: msdll_pack.py in.elf out.msdll', file=sys.stderr)
        return 1
    in_path, out_path = sys.argv[1], sys.argv[2]
    data = open(in_path, 'rb').read()
    elf = Elf(data)
    syms = elf.symbols()

    BASE = 0x64000000

    # Границы таблиц (в виртуальных адресах = RVA+base).
    def sym(name):
        if name not in syms:
            raise ValueError('symbol not found: ' + name)
        return syms[name]

    exp_va = sym('_msdll_exports_start')
    exp_end = sym('_msdll_exports_end')
    imp_va = sym('_msdll_imports_start')
    imp_end = sym('_msdll_imports_end')

    # Собираем образ: секции .text/.data/.bss + ropool + таблицы.
    # Простейший путь — вычислить image_size как max(addr+size) по всем
    # PROGBITS-секциям с addr >= BASE, и скопировать их в буфер.
    image_size = 0
    for s in elf.sh:
        if s['addr'] >= BASE and s['type'] != SHT_SYMTAB and s['type'] != SHT_REL:
            end = s['addr'] + s['size']
            if end > image_size:
                image_size = end
    image_size -= BASE
    if image_size <= 0:
        raise ValueError('no loadable sections above base')
    image_size = (image_size + 0xFFF) & ~0xFFF   # выравнивание до 4 КБ

    image = bytearray(image_size)

    def copy_section(s):
        if s['addr'] < BASE or s['size'] == 0:
            return
        if s['type'] in (SHT_SYMTAB, SHT_REL):
            return
        off = s['addr'] - BASE
        image[off:off + s['size']] = data[s['offset']:s['offset'] + s['size']]

    for s in elf.sh:
        copy_section(s)

    # Релокации.
    #
    # ПРОБЛЕМА: gcc/ld с -no-pie и фиксированной базой подставляет абсолютные
    # адреса (0x6400XXXX) как КОНСТАНТЫ прямо в код/данные, без R_386_32.
    # Значит, если DLL грузится не по 0x64000000, а со смещением
    # (bump-аллокатор даёт base+off), эти адреса станут неверными.
    #
    # РЕШЕНИЕ: сами сканируем образ на u32-значения, попадающие в диапазон
    # [BASE, BASE+image_size). Каждое — потенциальный абсолютный адрес,
    # который надо увеличить на (new_base - BASE). Это эвристика, но для
    # нашего формата (база фиксирована линкером) она работает надёжно:
    # случайных значений в этом узком диапазоне в коде почти не бывает.
    #
    # Дополнительно учитываем «настоящие» R_386_32 из .rel.* (если есть).
    relocs = []
    seen = set()

    def add_reloc(rva):
        if rva in seen:
            return
        if rva < 0 or (rva + 4) > image_size:
            return
        seen.add(rva)
        relocs.append((rva, 1))   # 1 = MSDLL_RELOC_ABS32

    # 1) Явные R_386_32.
    for r_offset, r_type in elf.relocs():
        if r_type != R_386_32:
            continue
        if r_offset < BASE:
            continue
        add_reloc(r_offset - BASE)

    # 2) Авто-скан: u32 в [BASE, BASE+image_size) — считаем абсолютным адресом.
    #
    # ИСКЛЮЧАЕМ диапазоны таблиц экспортов/импортов: там лежат func_rva
    # (RVA, а не абсолютные адреса) — их релоцировать нельзя.
    protect = []
    if exp_end > exp_va:
        protect.append((exp_va - BASE, exp_end - BASE))
    if imp_end > imp_va:
        protect.append((imp_va - BASE, imp_end - BASE))

    def protected(off):
        for a, b in protect:
            if a <= off < b:
                return True
        return False

    # Точные адреса символов: только они считаются «настоящими» абсолютными
    # ссылками. Это убирает ложные срабатывания и не требует выравнивания
    # (адрес внутри инструкции movabs может стоять не на границе 4 байт).
    valid_addrs = set()
    for nm, va in syms.items():
        if BASE <= va < BASE + image_size:
            valid_addrs.add(va)

    limit = BASE + image_size
    for off in range(0, image_size - 3):   # БЕЗ шага 4: ищем и невыровненные
        if protected(off):
            continue
        v = struct.unpack_from('<I', image, off)[0]
        if v in valid_addrs:
            add_reloc(off)

    relocs.sort()

    # Таблицы уже в образе (exports/imports). Пересчитываем их RVA.
    export_rva = exp_va - BASE if exp_end > exp_va else 0
    import_rva = imp_va - BASE if imp_end > imp_va else 0

    # Таблица релокаций: кладём в конец образа (после всех секций).
    reloc_rva = len(image)
    for rva, typ in relocs:
        image += struct.pack('<II', rva, typ)
    # Выравнивание.
    while len(image) & 3:
        image.append(0)

    # Флаги.
    flags = 0
    if relocs:
        flags |= FLAG_RELOCS
    entry_rva = elf.e_entry - BASE if elf.e_entry >= BASE else 0
    if entry_rva:
        flags |= FLAG_ENTRY

    # Заголовок (64 байта).
    hdr = struct.pack('<IIIIIIIIIIIIII',
        MSDLL_MAGIC, MSDLL_VERSION, flags,
        image_size, entry_rva,
        (exp_end - exp_va) // 8 if exp_end > exp_va else 0,   # export_count
        export_rva,
        (imp_end - imp_va) // 12 if imp_end > imp_va else 0,  # import_count
        import_rva,
        len(relocs), reloc_rva,
        0, 0,   # section_count, section_rva
        0)      # reserved[0]
    hdr = hdr.ljust(64, b'\0')

    out = hdr + bytes(image)
    open(out_path, 'wb').write(out)
    print('OK: %s (%d bytes, %d exports, %d imports, %d relocs)' % (
        out_path, len(out),
        (exp_end - exp_va) // 8 if exp_end > exp_va else 0,
        (imp_end - imp_va) // 12 if imp_end > imp_va else 0,
        len(relocs)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
