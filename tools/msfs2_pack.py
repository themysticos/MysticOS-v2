#!/usr/bin/env python3
# msfs2_pack.py - создать образ MSFS2 (Mystic Soft FS v2, маркерная).
#
# Формат:
#   LBA 0: СУПЕРБЛОК
#     "MSFS" | version(2) | block_size(4) | total_blocks(4) | root_lba(4)
#
#   Далее БЛОКИ (размер block_size, выровнены). Каждый блок:
#     [START] "MSFS" | 0x01 | namelen(1) | datalen(4) | name[namelen]
#             <данные: datalen байт>
#     [END]   "MSFS" | 0x02 | namelen(1) | next_lba(4) | name[namelen]
#
#   next_lba = 0 -> конец файла. Иначе продолжение в блоке next_lba.
#
# БОЛЬШИЕ ФАЙЛЫ разбиваются на несколько блоков (цепочка через next_lba).
#
# Использование:
#   python3 msfs2_pack.py <image> <src_dir> [block_size]
#   Каталог src_dir кладётся рекурсивно; пути сохраняются в именах
#   (например "kernel/SYSPR/TERM.ELF").

import sys, os, struct

MAGIC = b'MSFS'
T_START = 0x01
T_END   = 0x02
SB_LBA  = 0
DEFAULT_BLOCK = 4096


def u32(v): return struct.pack('<I', v)


class Packer:
    def __init__(self, block_size):
        self.bs = block_size
        # Условно считаем, что суперблок занимает первые N байт, дальше блоки.
        # Блоки адресуются по LBA (индекс блока), не по сектору.
        self.blocks = {}     # block_index -> bytes (ровно block_size)
        self.next_idx = 1    # блок 0 = суперблок

    def alloc(self):
        idx = self.next_idx
        self.next_idx += 1
        return idx

    # Записать данные файла как цепочку блоков. Возвращает первый индекс.
    def add_file(self, name, data):
        nameb = name.encode('utf-8')
        if len(nameb) > 255:
            raise SystemExit('name too long: ' + name)
        # Насколько данных влезает в блок (вычитаем START+END маркеры).
        start_len = 4 + 1 + 1 + 4 + len(nameb)
        end_len   = 4 + 1 + 1 + 4 + len(nameb)
        payload = self.bs - start_len - end_len
        if payload <= 0:
            raise SystemExit('block too small for name: ' + name)

        # Разбить data на блоки.
        chunks = []
        if len(data) == 0:
            chunks = [b'']
        else:
            off = 0
            while off < len(data):
                chunks.append(data[off:off + payload])
                off += payload

        # Выделить индексы блоков для всех чанков.
        idxs = [self.alloc() for _ in chunks]
        first = idxs[0]

        for i, chunk in enumerate(chunks):
            nxt = idxs[i + 1] if i + 1 < len(idxs) else 0
            blk = bytearray()
            # START
            blk += MAGIC + bytes([T_START, len(nameb)]) + u32(len(chunk)) + nameb
            blk += chunk
            # END (next_lba)
            blk += MAGIC + bytes([T_END, len(nameb)]) + u32(nxt) + nameb
            # Добить блок до размера block_size.
            if len(blk) > self.bs:
                raise SystemExit('internal: block overflow for ' + name)
            blk += b'\x00' * (self.bs - len(blk))
            self.blocks[idxs[i]] = bytes(blk)
        return first

    # Записать весь образ.
    def write(self, path, total_blocks):
        sb = bytearray()
        sb += MAGIC
        sb += struct.pack('<H', 2)          # version
        sb += u32(self.bs)                  # block_size
        sb += u32(total_blocks)             # total_blocks
        sb += u32(0)                        # root_lba (зарезервировано)
        sb += b'\x00' * (self.bs - len(sb)) # добить суперблок
        out = bytearray()
        out += sb
        # Блоки по порядку (0 = суперблок уже добавлен, начинаем с 1).
        for i in range(1, total_blocks):
            if i in self.blocks:
                out += self.blocks[i]
            else:
                out += b'\x00' * self.bs
        with open(path, 'wb') as f:
            f.write(out)


def walk(src, prefix, packer):
    entries = sorted(os.listdir(src))
    for name in entries:
        full = os.path.join(src, name)
        virt = (prefix + '/' + name) if prefix else name
        if os.path.isdir(full):
            # Каталог = блок-запись с datalen=0 (пустой), а дети — отдельные
            # файлы с полным путём. Так индекс в ядре строится по путям.
            packer.add_file(virt + '/', b'')
            walk(full, virt, packer)
        else:
            with open(full, 'rb') as f:
                data = f.read()
            packer.add_file(virt, data)


def main():
    if len(sys.argv) < 3:
        print('usage: msfs2_pack.py <image> <src_dir> [block_size]')
        return 1
    image = sys.argv[1]
    src = sys.argv[2]
    bs = int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_BLOCK
    # Размер образа: 128 МБ по умолчанию (как FAT-образ).
    img_size = 128 * 1024 * 1024
    if os.path.exists(image):
        img_size = os.path.getsize(image)
    total_blocks = img_size // bs
    packer = Packer(bs)
    walk(src, '', packer)
    packer.write(image, total_blocks)
    print(f'MSFS2: {len(packer.blocks)} blocks used, block={bs}, total={total_blocks}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
