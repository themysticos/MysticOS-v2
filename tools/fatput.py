#!/usr/bin/env python3
# fatput.py - положить файл в FAT16-образ MysticOS без mtools.
# Использование: python3 fatput.py <image> <local_file> <DEST.NAME> [PATH]
#   PATH — каталог внутри образа (например, "kernel/SYSPR" или "/kernel/SYSPR").
#          Каталоги создаются автоматически. Без PATH — корень.
# Понимает геометрию из BPB (в т.ч. нестандартную -s 64).

import sys, struct

SECTOR = 512
ATTR_DIR = 0x10
DIR_END = 0x00
DIR_FREE = 0xE5


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]


def make_83(name):
    name = name.upper()
    if '.' in name:
        base, ext = name.split('.', 1)
    else:
        base, ext = name, ''
    base = (base[:8]).ljust(8)
    ext = (ext[:3]).ljust(3)
    return (base + ext).encode('ascii')


class Image:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.img = bytearray(f.read())
        img = self.img
        self.bps = u16(img, 11)
        self.spc = img[13]
        self.reserved = u16(img, 14)
        self.nfats = img[16]
        self.root_ents = u16(img, 17)
        self.spf = u16(img, 22)
        if self.bps != SECTOR:
            raise SystemExit("error: only 512-byte sectors supported")
        self.fat_start = self.reserved
        self.root_start = self.reserved + self.nfats * self.spf
        self.root_sectors = (self.root_ents * 32 + SECTOR - 1) // SECTOR
        self.data_start = self.root_start + self.root_sectors
        self.cluster_size = self.spc * SECTOR
        self.fat_bytes = self.spf * SECTOR
        print(f"geometry: spc={self.spc} reserved={self.reserved} nfats={self.nfats} "
              f"spf={self.spf} root_ents={self.root_ents}")
        print(f"fat@{self.fat_start} root@{self.root_start} data@{self.data_start}")

    # --- FAT ---
    def fat_get(self, cl):
        off = self.fat_start * SECTOR + cl * 2
        return u16(self.img, off)

    def fat_set_all(self, cl, val):
        for k in range(self.nfats):
            base = (self.reserved + k * self.spf) * SECTOR
            struct.pack_into('<H', self.img, base + cl * 2, val)

    def alloc_chain(self, nclusters):
        free = []
        max_cl = (len(self.img) - self.data_start * SECTOR) // self.cluster_size + 2
        for cl in range(2, max_cl):
            if self.fat_get(cl) == 0:
                free.append(cl)
                if len(free) == nclusters:
                    break
        if len(free) < nclusters:
            raise SystemExit("error: not enough free clusters")
        for i, cl in enumerate(free):
            nxt = free[i+1] if i+1 < len(free) else 0xFFFF
            self.fat_set_all(cl, nxt)
        return free

    def write_chain(self, clusters, data):
        for i, cl in enumerate(clusters):
            chunk = data[i*self.cluster_size:(i+1)*self.cluster_size]
            chunk = chunk + b'\x00' * (self.cluster_size - len(chunk))
            off = self.data_start * SECTOR + (cl - 2) * self.cluster_size
            self.img[off:off+self.cluster_size] = chunk

    # --- каталоги ---
    def dir_region(self, dir_cluster):
        """Список (lba_start, lba_end) секторов каталога."""
        if dir_cluster == 0:
            return self.root_start, self.root_start + self.root_sectors
        start = self.data_start + (dir_cluster - 2) * self.spc
        return start, start + self.spc

    def find_in_dir(self, dir_cluster, name83):
        """Возвращает offset записи (в байтах образа) или None."""
        s, e = self.dir_region(dir_cluster)
        for sec in range(s, e):
            base = sec * SECTOR
            for ent in range(SECTOR // 32):
                off = base + ent * 32
                if self.img[off] == DIR_END:
                    return None
                if self.img[off] == DIR_FREE:
                    continue
                if bytes(self.img[off:off+11]) == name83:
                    return off
        return None

    def find_free_slot(self, dir_cluster):
        """Свободный слот (off) или None, если каталог полон."""
        s, e = self.dir_region(dir_cluster)
        for sec in range(s, e):
            base = sec * SECTOR
            for ent in range(SECTOR // 32):
                off = base + ent * 32
                if self.img[off] == DIR_END or self.img[off] == DIR_FREE:
                    return off
        return None

    def add_entry(self, dir_cluster, name83, attr, first_cluster, size):
        off = self.find_in_dir(dir_cluster, name83)
        if off is None:
            off = self.find_free_slot(dir_cluster)
        if off is None:
            raise SystemExit("error: directory full")
        rec = bytearray(32)
        rec[0:11] = name83
        rec[11] = attr
        struct.pack_into('<H', rec, 26, first_cluster & 0xFFFF)
        struct.pack_into('<H', rec, 20, (first_cluster >> 16) & 0xFFFF)
        struct.pack_into('<I', rec, 28, size)
        self.img[off:off+32] = rec
        return off

    def mkdir(self, parent_cluster, name):
        """Создаёт каталог name в parent_cluster, возвращает его кластер."""
        name83 = make_83(name)
        existing = self.find_in_dir(parent_cluster, name83)
        if existing is not None:
            fc = u16(self.img, existing + 26) | (u16(self.img, existing + 20) << 16)
            return fc
        cl = self.alloc_chain(1)[0]
        # содержимое нового каталога: . и ..
        dot = bytearray(32)
        dot[0:11] = b'.          '
        dot[11] = ATTR_DIR
        struct.pack_into('<H', dot, 26, cl)
        dotdot = bytearray(32)
        dotdot[0:11] = b'..         '
        dotdot[11] = ATTR_DIR
        struct.pack_into('<H', dotdot, 26, parent_cluster)
        data = bytes(dot) + bytes(dotdot)
        self.write_chain([cl], data)
        self.add_entry(parent_cluster, name83, ATTR_DIR, cl, 0)
        return cl

    def resolve_dir(self, path, create=False):
        """Возвращает кластер каталога по пути (корень=0)."""
        if not path:
            return 0
        cur = 0
        for comp in path.replace('\\', '/').split('/'):
            if comp == '' or comp == '.':
                continue
            name83 = make_83(comp)
            off = self.find_in_dir(cur, name83)
            if off is None:
                if not create:
                    raise SystemExit(f"error: no such directory: {comp}")
                cur = self.mkdir(cur, comp)
            else:
                cur = u16(self.img, off + 26) | (u16(self.img, off + 20) << 16)
        return cur

    def put_file(self, local_path, dest_name, path=''):
        with open(local_path, 'rb') as lf:
            filedata = lf.read()
        nclusters = max(1, (len(filedata) + self.cluster_size - 1) // self.cluster_size)
        print(f"file size={len(filedata)} needs {nclusters} clusters")
        dir_cluster = self.resolve_dir(path, create=True)
        clusters = self.alloc_chain(nclusters)
        self.write_chain(clusters, filedata)
        self.add_entry(dir_cluster, make_83(dest_name), 0x00, clusters[0], len(filedata))
        print(f"OK: {path or '/'}{dest_name} -> clusters {clusters[:8]}{'...' if len(clusters)>8 else ''}")

    def save(self):
        with open(self.path, 'wb') as f:
            f.write(self.img)


def main():
    if len(sys.argv) < 4:
        print("usage: fatput.py <image> <local_file> <DEST.NAME> [PATH]")
        return 1
    img_path, local_path, dest_name = sys.argv[1:4]
    path = sys.argv[4] if len(sys.argv) > 4 else ''
    im = Image(img_path)
    im.put_file(local_path, dest_name, path)
    im.save()
    return 0


if __name__ == '__main__':
    sys.exit(main())
