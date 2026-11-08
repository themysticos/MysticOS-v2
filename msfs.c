// msfs.c - MSFS2 (Mystic Soft File System v2): МАРКЕРНАЯ ФС.
//
// Формат (LBA 0 = СУПЕРБЛОК):
//   "MSFS" | version(2) | block_size(4) | total_blocks(4) | root_lba(4)
//          | free_lba(4) | free_list_head(4) | magic2(4)="MSFS"
//   Далее БЛОКИ (размер block_size = 4096). Каждый блок:
//     [START] "MSFS" | 0x01 | namelen(1) | datalen(4) | name
//             <данные: datalen байт>
//     [END]   "MSFS" | 0x02 | namelen(1) | next_lba(4) | name
//     [FREE_START] "MSFS" | 0x03 | namelen(1) | next_lba(4)
//     [FREE_END]   "MSFS" | 0x04 | namelen(1) | next_lba(4)
//   next_lba = 0 -> конец цепочки; иначе продолжение в блоке next_lba.
//
// СВОБОДНЫЕ БЛОКИ: удаляемый файл НЕ затирается — его START/END маркеры
//   заменяются на FREE_START/FREE_END, а next_lba сохраняются. Цепочка
//   встраивается в голову free-списка (суперблок хранит free_list_head).
//   Для FREE-блока next хранится по смещению 10 (4 байта).
//
// ПРИ ЗАГРУЗКЕ: сканируем блоки 1..free_lba, строим ИНДЕКС В ПАМЯТИ (pmem).
#include "msfs.h"
#include "ata.h"
#include "pmem.h"

void print_string(const char* str);
void put_char(char c);
void print_int(int num);

#define SECTOR_SIZE   512
#define MSFS_BS       4096
#define MSFS_VERSION  2
#define SECT_PER_BLK  (MSFS_BS / SECTOR_SIZE)

#define MT_START       0x01
#define MT_END         0x02
#define MT_FREE_START  0x03
#define MT_FREE_END    0x04

#define SB_MAGIC       0
#define SB_VERSION     4
#define SB_BLOCK_SIZE  6
#define SB_TOTAL       10
#define SB_ROOT_LBA    14
#define SB_FREE_LBA    18
#define SB_FREE_HEAD   22
#define SB_MAGIC2      26

#define MSFS_MAX_ENTRIES 1024
#define MSFS_NAME_MAX    96

typedef struct {
    char         name[MSFS_NAME_MAX];
    int          is_dir;
    unsigned int size;
    unsigned int first_lba;
} msfs_entry_t;

typedef struct {
    int           valid;
    unsigned int  block_size;
    unsigned int  total_blocks;
    unsigned int  free_lba;
    unsigned int  free_head;
    int           count;
    msfs_entry_t  entries[MSFS_MAX_ENTRIES];
} msfs_index_t;

static msfs_index_t* g_idx[ATA_MAX_DRIVES] = {0};
static int g_msfs_drive = 0;

#define IDX  g_idx[g_msfs_drive]

void msfs_use_drive(int d) {
    if (d < 0 || d >= ATA_MAX_DRIVES) d = 0;
    g_msfs_drive = d;
    ata_select(g_msfs_drive);
}
int msfs_current_drive(void) { return g_msfs_drive; }

static unsigned int rd_u32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static void wr_u32(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static int read_block(unsigned int idx, unsigned char* buf) {
    unsigned int lba = idx * SECT_PER_BLK;
    for (unsigned int s = 0; s < SECT_PER_BLK; s++)
        ata_read_sector(lba + s, buf + s * SECTOR_SIZE);
    return 1;
}

static int write_block(unsigned int idx, const unsigned char* buf) {
    unsigned int lba = idx * SECT_PER_BLK;
    for (unsigned int s = 0; s < SECT_PER_BLK; s++)
        ata_write_sector(lba + s, buf + s * SECTOR_SIZE);
    return 1;
}

static int str_eq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == 0 && *b == 0);
}
static int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }

static int is_msfs(const unsigned char* b) {
    return b[0] == 'M' && b[1] == 'S' && b[2] == 'F' && b[3] == 'S';
}

static int idx_find(const char* name) {
    if (!IDX) return -1;
    for (int i = 0; i < IDX->count; i++)
        if (str_eq(IDX->entries[i].name, name)) return i;
    return -1;
}
static int idx_alloc(const char* name) {
    if (!IDX) return -1;
    if (IDX->count >= MSFS_MAX_ENTRIES) return -1;
    int i = IDX->count++;
    msfs_entry_t* e = &IDX->entries[i];
    int k = 0; while (name[k] && k < MSFS_NAME_MAX - 1) { e->name[k] = name[k]; k++; }
    e->name[k] = 0;
    e->is_dir = 0; e->size = 0; e->first_lba = 0;
    return i;
}
static void idx_remove(int i) {
    if (!IDX || i < 0 || i >= IDX->count) return;
    for (int k = i; k < IDX->count - 1; k++)
        IDX->entries[k] = IDX->entries[k + 1];
    IDX->count--;
}

static int sb_read(unsigned char* blk) {
    read_block(0, blk);
    if (!is_msfs(blk)) return 0;
    return 1;
}

static void sb_write(void) {
    if (!IDX) return;
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) return;
    read_block(0, blk);
    wr_u32(blk + SB_FREE_LBA,  IDX->free_lba);
    wr_u32(blk + SB_FREE_HEAD, IDX->free_head);
    write_block(0, blk);
    pmem_free_block((unsigned int)blk);
}

static unsigned int free_alloc(void) {
    if (!IDX) return 0;
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) return 0;
    unsigned int got = 0;
    if (IDX->free_head != 0) {
        got = IDX->free_head;
        read_block(got, blk);
        unsigned int next = 0;
        // next у FREE-блока лежит ТАМ ЖЕ, где был END старого файла:
        // end_off = 10 + nlen + datalen, next = rd_u32(blk + end_off + 6).
        if (is_msfs(blk)) {
            unsigned int nl = blk[5];
            unsigned int dl = rd_u32(blk + 6);
            unsigned int eo = 10 + nl + dl;
            if (eo + 10 <= MSFS_BS) next = rd_u32(blk + eo + 6);
        }
        IDX->free_head = next;
        sb_write();
    } else {
        if (IDX->free_lba == 0 || IDX->free_lba >= IDX->total_blocks) {
            pmem_free_block((unsigned int)blk);
            return 0;
        }
        got = IDX->free_lba++;
        sb_write();
    }
    pmem_free_block((unsigned int)blk);
    return got;
}

static void free_push_chain(unsigned int first, unsigned int last) {
    if (!IDX || first == 0) return;
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) return;
    read_block(last, blk);
    if (is_msfs(blk)) {
        if (blk[4] == MT_START) blk[4] = MT_FREE_START;
        // next пишем ТАМ ЖЕ, где был END: end_off + 6.
        unsigned int nl = blk[5];
        unsigned int dl = rd_u32(blk + 6);
        unsigned int eo = 10 + nl + dl;
        if (eo + 10 <= MSFS_BS) wr_u32(blk + eo + 6, IDX->free_head);
        write_block(last, blk);
    }
    IDX->free_head = first;
    sb_write();
    pmem_free_block((unsigned int)blk);
}

static void msfs_scan(void) {
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) { print_string("[msfs] no mem for scan buf\n"); return; }
    if (!sb_read(blk)) {
        print_string("[msfs] bad superblock\n");
        pmem_free_block((unsigned int)blk);
        return;
    }
    unsigned int total = rd_u32(blk + SB_TOTAL);
    IDX->block_size   = rd_u32(blk + SB_BLOCK_SIZE);
    IDX->total_blocks = total;
    IDX->free_lba     = rd_u32(blk + SB_FREE_LBA);
    IDX->free_head    = rd_u32(blk + SB_FREE_HEAD);
    if (IDX->free_lba == 0) IDX->free_lba = 1;

    unsigned int hi = IDX->free_lba;
    if (hi > total) hi = total;
    for (unsigned int b = 1; b < hi; b++) {
        read_block(b, blk);
        if (!is_msfs(blk)) continue;
        unsigned char type = blk[4];
        if (type != MT_START) continue;
        unsigned int nlen = blk[5];
        unsigned int datalen = rd_u32(blk + 6);
        if (nlen == 0 || nlen >= MSFS_NAME_MAX) continue;
        char name[MSFS_NAME_MAX];
        for (unsigned int k = 0; k < nlen; k++) name[k] = (char)blk[10 + k];
        name[nlen] = 0;
        int is_dir = (nlen > 0 && name[nlen-1] == '/');
        if (is_dir) name[nlen-1] = 0;

        int ei = idx_find(name);
        if (ei < 0) ei = idx_alloc(name);
        if (ei < 0) continue;
        msfs_entry_t* e = &IDX->entries[ei];
        e->is_dir = is_dir;
        if (e->first_lba == 0) e->first_lba = b;
        e->size += datalen;
    }
    IDX->valid = 1;
    pmem_free_block((unsigned int)blk);
}

int msfs_probe(void) {
    if (!IDX) {
        IDX = (msfs_index_t*)(unsigned int)pmem_alloc_block();
        if (!IDX) return 0;
        IDX->valid = 0; IDX->count = 0;
    }
    if (!IDX->valid) msfs_scan();
    return IDX->valid;
}

void msfs_format(void) {
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) { print_string("[msfs] format: no mem\n"); return; }
    for (int i = 0; i < MSFS_BS; i++) blk[i] = 0;
    blk[0] = 'M'; blk[1] = 'S'; blk[2] = 'F'; blk[3] = 'S';
    blk[4] = MSFS_VERSION & 0xFF; blk[5] = (MSFS_VERSION >> 8) & 0xFF;
    wr_u32(blk + SB_BLOCK_SIZE, MSFS_BS);
    // Размер диска в блоках — из ATA IDENTIFY (capacity в секторах).
    unsigned int secs = ata_total_sectors(g_msfs_drive);
    unsigned int total = (secs ? (secs / SECT_PER_BLK) : 32768u);
    wr_u32(blk + SB_TOTAL, total);
    wr_u32(blk + SB_ROOT_LBA, 0);
    wr_u32(blk + SB_FREE_LBA, 1);
    wr_u32(blk + SB_FREE_HEAD, 0);
    wr_u32(blk + SB_MAGIC2, 0x5346534Du);
    write_block(0, blk);
    pmem_free_block((unsigned int)blk);
    if (IDX) { IDX->valid = 0; IDX->count = 0; }
    msfs_probe();
    print_string("[msfs] formatted\n");
}

int msfs_find(const char* path) {
    if (!IDX || !IDX->valid) return 0;
    const char* p = path; if (p[0] == '/') p++;
    return idx_find(p) >= 0 ? 1 : 0;
}

int msfs_is_dir_inode(int inode) {
    if (!IDX || !IDX->valid) return 0;
    if (inode < 0 || inode >= IDX->count) return 0;
    return IDX->entries[inode].is_dir;
}

int msfs_size(const char* path) {
    if (!IDX || !IDX->valid) return -1;
    const char* p = path; if (p[0] == '/') p++;
    int ei = idx_find(p);
    if (ei < 0 || IDX->entries[ei].is_dir) return -1;
    return (int)IDX->entries[ei].size;
}

int msfs_read_dir(const char* path, char* names, int* is_dir,
                  unsigned int* sizes, int max) {
    if (!IDX || !IDX->valid) return -1;
    const char* p = path; while (p[0] == '/') p++;
    int plen = str_len(p);
    int count = 0;
    for (int i = 0; i < IDX->count && count < max; i++) {
        const char* nm = IDX->entries[i].name;
        int ok = 1;
        for (int k = 0; k < plen; k++) if (nm[k] != p[k]) { ok = 0; break; }
        if (!ok) continue;
        if (plen > 0 && nm[plen] != '/') continue;
        const char* child = nm + (plen > 0 ? plen + 1 : 0);
        if (child[0] == 0) continue;
        int has_slash = 0;
        for (int k = 0; child[k]; k++) if (child[k] == '/') { has_slash = 1; break; }
        if (has_slash) continue;
        int k = 0; while (child[k] && k < 12) { names[count*13 + k] = child[k]; k++; }
        names[count*13 + k] = 0;
        is_dir[count] = IDX->entries[i].is_dir;
        sizes[count] = IDX->entries[i].size;
        count++;
    }
    return count;
}

int msfs_read_file(const char* path, unsigned char* buf, unsigned int max) {
    if (!IDX || !IDX->valid) return -1;
    const char* p = path; while (p[0] == '/') p++;
    int ei = idx_find(p);
    if (ei < 0 || IDX->entries[ei].is_dir) return -1;
    msfs_entry_t* e = &IDX->entries[ei];
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) return -1;
    unsigned int written = 0;
    unsigned int b = e->first_lba;
    while (b != 0 && written < max) {
        read_block(b, blk);
        if (!is_msfs(blk) || blk[4] != MT_START) break;
        unsigned int nlen = blk[5];
        unsigned int datalen = rd_u32(blk + 6);
        const unsigned char* data = blk + 10 + nlen;
        for (unsigned int k = 0; k < datalen && written < max; k++)
            buf[written++] = data[k];
        unsigned int end_off = 10 + nlen + datalen;
        if (end_off + 10 > MSFS_BS) break;
        unsigned int next = rd_u32(blk + end_off + 6);
        b = next;
    }
    pmem_free_block((unsigned int)blk);
    return (int)written;
}

// Освободить цепочку блоков файла (пометить FREE_*) и встроить в free-список.
static void release_chain(unsigned int b) {
    if (b == 0 || !IDX) return;
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) return;
    unsigned int last = b, cur = b;
    while (cur != 0) {
        read_block(cur, blk);
        if (!is_msfs(blk)) break;
        if (blk[4] == MT_START) {
            blk[4] = MT_FREE_START;
            unsigned int nl = blk[5];
            unsigned int dl = rd_u32(blk + 6);
            unsigned int eo = 10 + nl + dl;
            if (eo + 10 <= MSFS_BS && is_msfs(blk + eo) && blk[eo+4] == MT_END)
                blk[eo+4] = MT_FREE_END;
            write_block(cur, blk);
        }
        unsigned int nl = blk[5];
        unsigned int dl = rd_u32(blk + 6);
        unsigned int eo = 10 + nl + dl;
        unsigned int nxt = (eo + 10 <= MSFS_BS) ? rd_u32(blk + eo + 6) : 0;
        last = cur;
        cur = nxt;
    }
    free_push_chain(b, last);
    pmem_free_block((unsigned int)blk);
}

static int msfs_write_impl(const char* path, const char* content, int is_dir) {
    if (!IDX || !IDX->valid) return 0;
    const char* p = path; while (p[0] == '/') p++;
    int nlen = str_len(p);
    if (nlen == 0 || nlen >= MSFS_NAME_MAX - 2) return 0;

    unsigned int datalen = 0;
    if (content && !is_dir) datalen = (unsigned int)str_len(content);

    int old = idx_find(p);
    if (old >= 0) {
        release_chain(IDX->entries[old].first_lba);
        idx_remove(old);
    }

    int ei = idx_alloc(p);
    if (ei < 0) return 0;
    msfs_entry_t* e = &IDX->entries[ei];
    e->is_dir = is_dir;
    e->size = datalen;

    unsigned int max_data = MSFS_BS - 20 - (unsigned int)nlen - (is_dir ? 1 : 0);
    if (is_dir) datalen = 0;

    unsigned int first = 0, prev = 0, done = 0;
    unsigned char* blk = (unsigned char*)(unsigned int)pmem_alloc_block();
    if (!blk) { idx_remove(ei); return 0; }

    do {
        unsigned int chunk = datalen - done;
        if (chunk > max_data) chunk = max_data;
        unsigned int bl = free_alloc();
        if (bl == 0) { pmem_free_block((unsigned int)blk); idx_remove(ei); return 0; }
        if (first == 0) { first = bl; e->first_lba = bl; }

        for (int i = 0; i < MSFS_BS; i++) blk[i] = 0;
        blk[0] = 'M'; blk[1] = 'S'; blk[2] = 'F'; blk[3] = 'S';
        blk[4] = MT_START;
        unsigned int nl = (unsigned int)nlen + (is_dir ? 1 : 0);
        blk[5] = (unsigned char)nl;
        wr_u32(blk + 6, chunk);
        unsigned int off = 10;
        for (int k = 0; k < nlen; k++) blk[off++] = (unsigned char)p[k];
        if (is_dir) blk[off++] = '/';
        if (!is_dir && content) {
            for (unsigned int k = 0; k < chunk; k++) blk[off + k] = (unsigned char)content[done + k];
        }
        off += chunk;
        blk[off+0] = 'M'; blk[off+1] = 'S'; blk[off+2] = 'F'; blk[off+3] = 'S';
        blk[off+4] = MT_END;
        blk[off+5] = (unsigned char)nl;
        wr_u32(blk + off + 6, 0);
        write_block(bl, blk);

        if (prev != 0) {
            read_block(prev, blk);
            unsigned int pnl = blk[5];
            unsigned int pdl = rd_u32(blk + 6);
            unsigned int peo = 10 + pnl + pdl;
            wr_u32(blk + peo + 6, bl);
            write_block(prev, blk);
        }
        prev = bl;
        done += chunk;
    } while (done < datalen);

    pmem_free_block((unsigned int)blk);
    sb_write();
    return 1;
}

int msfs_write(const char* path, const char* content) {
    return msfs_write_impl(path, content, 0);
}
int msfs_mkdir(const char* path) {
    return msfs_write_impl(path, 0, 1);
}

int msfs_delete(const char* path) {
    if (!IDX || !IDX->valid) return 0;
    const char* p = path; while (p[0] == '/') p++;
    int ei = idx_find(p);
    if (ei < 0) return 0;
    if (IDX->entries[ei].is_dir) return 0;
    release_chain(IDX->entries[ei].first_lba);
    idx_remove(ei);
    return 1;
}

void msfs_list_print(const char* path) {
    if (!IDX || !IDX->valid) { print_string("msfs: not ready\n"); return; }
    char names[64 * 13]; int isdir[64]; unsigned int sizes[64];
    int n = msfs_read_dir(path, names, isdir, sizes, 64);
    print_string("Directory: ");
    print_string(path && path[0] ? path : "/");
    print_string("\n------------------\n");
    for (int i = 0; i < n; i++) {
        print_string("  ");
        print_string(names + i * 13);
        if (isdir[i]) print_string("/");
        else { print_string("  "); print_int((int)sizes[i]); print_string(" bytes"); }
        put_char('\n');
    }
    if (n == 0) print_string("  (empty)\n");
    print_string("------------------\n");
}
