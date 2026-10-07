// fat.c - драйвер FAT12/FAT16 для MysticOS с поддержкой подкаталогов

#define SECTOR_SIZE 512
#include "fat.h"
#include "ata.h"

typedef struct {
    unsigned char name[11];
    unsigned char attr;
    unsigned char reserved1, reserved2;
    unsigned short create_time, create_date, access_date;
    unsigned short first_cluster_high;
    unsigned short modify_time, modify_date;
    unsigned short first_cluster;
    unsigned int file_size;
} __attribute__((packed)) fat_entry_t;

typedef struct {
    unsigned char jmp[3];
    unsigned char oem_name[8];
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char fat_count;
    unsigned short root_entry_count;
    unsigned short total_sectors_16;
    unsigned char media_descriptor;
    unsigned short sectors_per_fat_16;
    unsigned short sectors_per_track, head_count;
    unsigned int hidden_sectors, total_sectors_32;
} __attribute__((packed)) fat_bpb_t;

void print_string(const char* str);
void put_char(char c);
void print_int(int num);
extern void ata_read_sector(unsigned int lba, unsigned char* buffer);
extern void ata_read_sectors(unsigned int lba, unsigned int count, unsigned char* buffer);
extern void ata_write_sector(unsigned int lba, const unsigned char* buffer);

#define MAX_CLUSTERS_PER_FILE 256
#define FAT_PATH_MAX 128

typedef struct {
    int valid;
    unsigned int reserved_sectors, fat_count, sectors_per_fat;
    unsigned int root_entry_count, root_dir_sector, root_dir_sectors_count;
    unsigned int data_start_sector, total_clusters, sectors_per_cluster;
    int is_fat16;
} fat_geom_t;

// Геометрия — per-drive (до 4 дисков ATA). Индекс 0..3.
#include "ata.h"
static fat_geom_t g_geom[ATA_MAX_DRIVES];
static int g_fat_drive = 0;   // текущий диск для операций FAT

// Выбрать диск для последующих операций FAT (D0..D3).
void fat_use_drive(int d) {
    if (d < 0 || d >= ATA_MAX_DRIVES) d = 0;
    g_fat_drive = d;
    ata_select(g_fat_drive);
}
int fat_current_drive(void) { return g_fat_drive; }

static void fat_load_geom(void) {
    fat_geom_t* gg = &g_geom[g_fat_drive];
    if (gg->valid) return;
    ata_select(g_fat_drive);
    unsigned char buf[SECTOR_SIZE];
    ata_read_sector(0, buf);
    fat_bpb_t* bpb = (fat_bpb_t*)buf;
    gg->reserved_sectors    = bpb->reserved_sectors;
    gg->fat_count           = bpb->fat_count;
    gg->sectors_per_fat     = bpb->sectors_per_fat_16;
    gg->root_entry_count    = bpb->root_entry_count;
    gg->sectors_per_cluster = bpb->sectors_per_cluster ? bpb->sectors_per_cluster : 1;
    unsigned int total = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    gg->root_dir_sector = gg->reserved_sectors + (gg->fat_count * gg->sectors_per_fat);
    gg->root_dir_sectors_count = (gg->root_entry_count * 32) / SECTOR_SIZE;
    // Сектор, с которого начинается кластер 2 (начало области данных)
    gg->data_start_sector = gg->root_dir_sector + gg->root_dir_sectors_count;
    unsigned int ds = total - (gg->reserved_sectors +
        (gg->fat_count * gg->sectors_per_fat) + gg->root_dir_sectors_count);
    gg->total_clusters = ds / gg->sectors_per_cluster;
    gg->is_fat16 = (gg->total_clusters >= 4085);
    gg->valid = 1;
}

// Хелпер: ссылка на геометрию ТЕКУЩЕГО диска (после fat_load_geom).
#define G (&g_geom[g_fat_drive])

static unsigned int fat12_get(unsigned int fat_start, unsigned int fat_sectors, unsigned int c) {
    unsigned int fo = (c * 3) / 2;
    unsigned int si = fo / SECTOR_SIZE, so = fo % SECTOR_SIZE;
    unsigned char fb[SECTOR_SIZE];
    ata_read_sector(fat_start + si, fb);
    if (so < SECTOR_SIZE - 1) {
        unsigned short v = *(unsigned short*)&fb[so];
        return (c & 1) ? (v >> 4) : (v & 0x0FFF);
    } else {
        unsigned char nb[SECTOR_SIZE];
        if (si + 1 < fat_sectors) ata_read_sector(fat_start + si + 1, nb); else nb[0] = 0;
        unsigned short v = fb[so] | (nb[0] << 8);
        return (c & 1) ? (v >> 4) : (v & 0x0FFF);
    }
}

static void fat12_set(unsigned int fat_start, unsigned int fat_sectors, unsigned int c, unsigned int value) {
    unsigned int fo = (c * 3) / 2;
    unsigned int si = fo / SECTOR_SIZE, so = fo % SECTOR_SIZE;
    unsigned char fb[SECTOR_SIZE];
    ata_read_sector(fat_start + si, fb);
    if (so < SECTOR_SIZE - 1) {
        unsigned short cur = *(unsigned short*)&fb[so];
        if (c & 1) cur = (cur & 0x000F) | ((value & 0x0FFF) << 4);
        else       cur = (cur & 0xF000) | (value & 0x0FFF);
        *(unsigned short*)&fb[so] = cur;
        ata_write_sector(fat_start + si, fb);
    } else {
        if (c & 1) fb[so] = (fb[so] & 0x0F) | ((value & 0x0F) << 4);
        else       fb[so] = (value & 0xFF);
        ata_write_sector(fat_start + si, fb);
        if (si + 1 < fat_sectors) {
            unsigned char nb[SECTOR_SIZE];
            ata_read_sector(fat_start + si + 1, nb);
            if (c & 1) nb[0] = (value >> 4) & 0xFF;
            else       nb[0] = (nb[0] & 0xF0) | ((value >> 8) & 0x0F);
            ata_write_sector(fat_start + si + 1, nb);
        }
    }
}

// Кэш FAT-сектора: соседние кластеры в одном секторе FAT -> читаем 1 раз.
static unsigned int g_fat_cache_lba = 0xFFFFFFFFu;
static unsigned char g_fat_cache[SECTOR_SIZE];

static unsigned int fat_next_cluster(unsigned int cluster) {
    fat_load_geom();
    unsigned int fat_start = G->reserved_sectors;
    if (G->is_fat16) {
        unsigned int lba = fat_start + (cluster * 2) / SECTOR_SIZE;
        unsigned int off = (cluster * 2) % SECTOR_SIZE;
        if (lba != g_fat_cache_lba) {
            ata_read_sector(lba, g_fat_cache);
            g_fat_cache_lba = lba;
        }
        return *(unsigned short*)&g_fat_cache[off];
    }
    return fat12_get(fat_start, G->sectors_per_fat, cluster);
}

// Конец цепочки (EOC): FAT12 -> >=0xFF8, FAT16 -> >=0xFFF8.
static int fat_is_eoc(unsigned int c) {
    fat_load_geom();
    return G->is_fat16 ? (c >= 0xFFF8u) : (c >= 0xFF8u);
}

static int fat_alloc_chain(unsigned int needed, unsigned int* chain) {
    fat_load_geom();
    unsigned int fat_start = G->reserved_sectors;
    unsigned int found = 0;
    unsigned int fat_bytes = G->sectors_per_fat * SECTOR_SIZE;
    unsigned int total_entries = G->is_fat16 ? (fat_bytes / 2) : ((fat_bytes * 2) / 3);
    for (unsigned int c = 2; c < total_entries && found < needed; c++) {
        unsigned int val;
        if (G->is_fat16) {
            unsigned int lba = fat_start + (c * 2) / SECTOR_SIZE;
            unsigned int off = (c * 2) % SECTOR_SIZE;
            unsigned char buf[SECTOR_SIZE];
            ata_read_sector(lba, buf);
            val = *(unsigned short*)&buf[off];
        } else {
            val = fat12_get(fat_start, G->sectors_per_fat, c);
        }
        if (val == 0x0000) chain[found++] = c;
    }
    return (found == needed) ? 1 : 0;
}

static void fat_write_chain(unsigned int* chain, unsigned int n) {
    fat_load_geom();
    unsigned int fat_start = G->reserved_sectors;
    for (unsigned int i = 0; i < n; i++) {
        unsigned int c = chain[i];
        unsigned int next = (i + 1 < n) ? chain[i + 1] : 0xFFFF;
        for (unsigned int f = 0; f < G->fat_count; f++) {
            unsigned int base = fat_start + f * G->sectors_per_fat;
            if (G->is_fat16) {
                unsigned int lba = base + (c * 2) / SECTOR_SIZE;
                unsigned int off = (c * 2) % SECTOR_SIZE;
                unsigned char buf[SECTOR_SIZE];
                ata_read_sector(lba, buf);
                *(unsigned short*)&buf[off] = (unsigned short)next;
                ata_write_sector(lba, buf);
            } else {
                fat12_set(base, G->sectors_per_fat, c, next);
            }
        }
    }
}

static void fat_free_chain(unsigned int first_cluster) {
    fat_load_geom();
    unsigned int c = first_cluster;
    while (c >= 2 && !fat_is_eoc(c)) {
        unsigned int next = fat_next_cluster(c);
        for (unsigned int f = 0; f < G->fat_count; f++) {
            unsigned int base = G->reserved_sectors + f * G->sectors_per_fat;
            if (G->is_fat16) {
                unsigned int lba = base + (c * 2) / SECTOR_SIZE;
                unsigned int off = (c * 2) % SECTOR_SIZE;
                unsigned char buf[SECTOR_SIZE];
                ata_read_sector(lba, buf);
                *(unsigned short*)&buf[off] = 0x0000;
                ata_write_sector(lba, buf);
            } else {
                fat12_set(base, G->sectors_per_fat, c, 0x000);
            }
        }
        if (next < 2 || fat_is_eoc(next)) break;
        c = next;
    }
}

static void fat_make_name(const char* filename, unsigned char* out) {
    for (int k = 0; k < 11; k++) out[k] = ' ';
    int i = 0, j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = (unsigned char)c; i++;
    }
    if (filename[i] == '.') {
        i++; j = 8;
        while (filename[i] && j < 11) {
            char c = filename[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[j++] = (unsigned char)c; i++;
        }
    }
}

static int fat_dir_lookup(unsigned int dir, const unsigned char* fat_name,
                          unsigned int* out_sector, int* out_idx,
                          unsigned int* out_fc, unsigned int* out_size, unsigned char* out_attr) {
    fat_load_geom();
    unsigned int sector, end_sector;
    if (dir == 0) { sector = G->root_dir_sector; end_sector = sector + G->root_dir_sectors_count; }
    else { sector = G->data_start_sector + (dir - 2) * G->sectors_per_cluster; end_sector = sector + G->sectors_per_cluster; }
    unsigned char buf[SECTOR_SIZE];
    while (sector < end_sector) {
        ata_read_sector(sector, buf);
        for (int e = 0; e < 16; e++) {
            fat_entry_t* ent = (fat_entry_t*)&buf[e * 32];
            if (ent->name[0] == 0x00) return 0;
            if (ent->name[0] == 0xE5) continue;
            if (ent->attr & 0x08) continue;
            if (ent->name[0] == '.') continue;
            int match = 1;
            for (int k = 0; k < 11; k++) if (ent->name[k] != fat_name[k]) { match = 0; break; }
            if (match) {
                if (out_sector) *out_sector = sector;
                if (out_idx) *out_idx = e;
                if (out_fc) *out_fc = ent->first_cluster | (ent->first_cluster_high << 16);
                if (out_size) *out_size = ent->file_size;
                if (out_attr) *out_attr = ent->attr;
                return 1;
            }
        }
        if (dir == 0) { sector++; continue; }
        unsigned int nxt = fat_next_cluster(dir);
        if (nxt < 2 || fat_is_eoc(nxt)) break;
        dir = nxt;
        sector = G->data_start_sector + (dir - 2) * G->sectors_per_cluster;
        end_sector = sector + G->sectors_per_cluster;
    }
    return 0;
}

static int fat_dir_alloc_slot(unsigned int dir, unsigned int* out_sector, int* out_idx) {
    fat_load_geom();
    unsigned int sector, end_sector;
    if (dir == 0) { sector = G->root_dir_sector; end_sector = sector + G->root_dir_sectors_count; }
    else { sector = G->data_start_sector + (dir - 2) * G->sectors_per_cluster; end_sector = sector + G->sectors_per_cluster; }
    unsigned char buf[SECTOR_SIZE];
    for (; sector < end_sector; sector++) {
        ata_read_sector(sector, buf);
        for (int e = 0; e < 16; e++) {
            fat_entry_t* ent = (fat_entry_t*)&buf[e * 32];
            if (ent->name[0] == 0x00 || ent->name[0] == 0xE5) { *out_sector = sector; *out_idx = e; return 1; }
        }
    }
    return 0;
}

static int fat_resolve(int base_dir, const char* path,
                       unsigned int* out_dir, unsigned int* out_fc,
                       unsigned int* out_size, unsigned char* out_attr) {
    fat_load_geom();
    unsigned int dir = (unsigned int)base_dir;
    unsigned int fc = 0, size = 0; unsigned char attr = 0;
    int i = 0;
    if (path[0] == '/') { dir = 0; i = 1; }
    char comp[64];
    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == 0) break;
        int c = 0;
        while (path[i] && path[i] != '/' && c < 63) comp[c++] = path[i++];
        comp[c] = 0;
        unsigned char nm[11];
        fat_make_name(comp, nm);
        if (!fat_dir_lookup(dir, nm, 0, 0, &fc, &size, &attr)) return 0;
        int j = i; while (path[j] == '/') j++;
        if (path[j]) {
            if (!(attr & 0x10)) return 0;
            dir = fc;
        }
    }
    if (out_dir) *out_dir = dir;
    if (out_fc) *out_fc = fc;
    if (out_size) *out_size = size;
    if (out_attr) *out_attr = attr;
    return 1;
}

int fat_find(int base_dir, const char* path) {
    return fat_resolve(base_dir, path, 0, 0, 0, 0);
}

// Читает весь файл в буфер. Возвращает число байт или -1.
int fat_read_file_buf(const char* path, unsigned char* buf, unsigned int max) {
    unsigned int fc, sz; unsigned char at;
    if (!fat_resolve(0, path, 0, &fc, &sz, &at)) return -1;
    if (at & 0x10) return -1;   // это каталог
    fat_load_geom();
    unsigned int written = 0;
    unsigned int cluster = fc;
    unsigned char sec[SECTOR_SIZE];
    // ВАЖНО: 0xFFF - конец ТОЛЬКО для FAT12! В FAT16 конец = 0xFFF8..0xFFFF.
    // Раньше обрывались на нормальном кластере 4095 (0xFFF).
    while (written < sz && written < max && cluster >= 2 && cluster < 0xFFF8) {
        // Мультисектор: несколько секторов за одну ATA-команду.
        unsigned int base_lba = G->data_start_sector + (cluster - 2) * G->sectors_per_cluster;
        unsigned int need = sz - written;
        if (need > max - written) need = max - written;
        unsigned int secs = (need + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (secs > G->sectors_per_cluster) secs = G->sectors_per_cluster;
        if (secs > 255) secs = 255;
        // Читаем по секторам, копируя РОВНО нужное (не переполнять buf!).
        for (unsigned int s = 0; s < secs && written < sz && written < max; s++) {
            ata_read_sector(base_lba + s, sec);
            unsigned int n = SECTOR_SIZE;
            if (written + n > sz)  n = sz - written;
            if (written + n > max) n = max - written;
            for (unsigned int k = 0; k < n; k++) buf[written + k] = sec[k];
            written += n;
        }
        unsigned int nxt = fat_next_cluster(cluster);
        if (nxt < 2 || nxt >= 0xFFF8) break;   // FAT16: конец = 0xFFF8..0xFFFF
        cluster = nxt;
    }
    return (int)written;
}

// Заполняет массивы списком каталога (для эксплорера).
int fat_read_dir(int base_dir, const char* path, char* names, int* is_dir,
                 unsigned int* sizes, int max) {
    unsigned int dir = (unsigned int)base_dir;
    if (path && path[0] && !(path[0] == '/' && path[1] == 0)) {
        unsigned int fc, sz; unsigned char at;
        if (!fat_resolve(base_dir, path, &dir, &fc, &sz, &at)) return 0;
        if (!(at & 0x10)) return 0;
        dir = fc;
    }
    fat_load_geom();
    int count = 0;
    unsigned int sector, end_sector;
    if (dir == 0) { sector = G->root_dir_sector; end_sector = sector + G->root_dir_sectors_count; }
    else { sector = G->data_start_sector + (dir - 2) * G->sectors_per_cluster; end_sector = sector + G->sectors_per_cluster; }
    unsigned char buf[SECTOR_SIZE];
    while (sector < end_sector && count < max) {
        ata_read_sector(sector, buf);
        for (int e = 0; e < 16 && count < max; e++) {
            fat_entry_t* ent = (fat_entry_t*)&buf[e * 32];
            if (ent->name[0] == 0x00) return count;
            if (ent->name[0] == 0xE5) continue;
            if (ent->attr & 0x08) continue;
            if (ent->attr & 0x02) continue;
            if (ent->attr & 0x04) continue;
            if (ent->name[0] == '.') continue;
            char* nm = names + count * 13;
            int p = 0;
            for (int k = 0; k < 8; k++) { if (ent->name[k] == ' ') break; nm[p++] = (char)ent->name[k]; }
            int has_ext = 0;
            for (int k = 8; k < 11; k++) if (ent->name[k] != ' ') { has_ext = 1; break; }
            if (has_ext) {
                nm[p++] = '.';
                for (int k = 8; k < 11; k++) { if (ent->name[k] == ' ') break; nm[p++] = (char)ent->name[k]; }
            }
            nm[p] = 0;
            is_dir[count] = (ent->attr & 0x10) ? 1 : 0;
            sizes[count] = ent->file_size;
            count++;
        }
        if (dir == 0) { sector++; continue; }
        unsigned int nxt = fat_next_cluster(dir);
        if (nxt < 2 || fat_is_eoc(nxt)) break;
        dir = nxt;
        sector = G->data_start_sector + (dir - 2) * G->sectors_per_cluster;
        end_sector = sector + G->sectors_per_cluster;
    }
    return count;
}

void fat_list(int base_dir, const char* path) {
    unsigned int dir = (unsigned int)base_dir;
    // Корень ("/" или пусто) — сразу dir=0, без resolve.
    if (path && path[0] && !(path[0] == '/' && path[1] == 0)) {
        unsigned int fc, sz; unsigned char at;
        if (!fat_resolve(base_dir, path, &dir, &fc, &sz, &at)) {
            print_string("ls: no such directory\n"); return;
        }
        if (!(at & 0x10)) { print_string("ls: not a directory\n"); return; }
        dir = fc;
    }
    fat_load_geom();
    print_string("Directory listing:\n------------------\n");
    int count = 0;
    unsigned int sector, end_sector;
    if (dir == 0) { sector = G->root_dir_sector; end_sector = sector + G->root_dir_sectors_count; }
    else { sector = G->data_start_sector + (dir - 2) * G->sectors_per_cluster; end_sector = sector + G->sectors_per_cluster; }
    unsigned char buf[SECTOR_SIZE];
    while (sector < end_sector) {
        ata_read_sector(sector, buf);
        for (int e = 0; e < 16; e++) {
            fat_entry_t* ent = (fat_entry_t*)&buf[e * 32];
            if (ent->name[0] == 0x00) goto done;
            if (ent->name[0] == 0xE5) continue;
            if (ent->attr & 0x08) continue;
            if (ent->attr & 0x02) continue;
            if (ent->attr & 0x04) continue;
            if (ent->name[0] == '.') continue;
            print_string("  ");
            for (int k = 0; k < 8; k++) { if (ent->name[k] == ' ') break; put_char((char)ent->name[k]); }
            int has_ext = 0;
            for (int k = 8; k < 11; k++) if (ent->name[k] != ' ') { has_ext = 1; break; }
            if (has_ext) {
                put_char('.');
                for (int k = 8; k < 11; k++) { if (ent->name[k] == ' ') break; put_char((char)ent->name[k]); }
            }
            if (ent->attr & 0x10) {
                print_string("/");
            } else {
                int nl = 0;
                for (int k = 0; k < 8; k++) { if (ent->name[k] == ' ') break; nl++; }
                if (has_ext) { nl++; for (int k = 8; k < 11; k++) { if (ent->name[k] == ' ') break; nl++; } }
                for (int k = nl; k < 20; k++) put_char(' ');
                print_int(ent->file_size);
                print_string(" bytes");
            }
            put_char('\n');
            count++;
        }
        if (dir == 0) { sector++; continue; }
        unsigned int nxt = fat_next_cluster(dir);
        if (nxt < 2 || fat_is_eoc(nxt)) break;
        dir = nxt;
        sector = G->data_start_sector + (dir - 2) * G->sectors_per_cluster;
        end_sector = sector + G->sectors_per_cluster;
    }
done:
    if (count == 0) print_string("  (empty)\n");
    else { print_string("------------------\nTotal: "); print_int(count); print_string(" item(s)\n"); }
}

void fat_read(int base_dir, const char* path) {
    unsigned int fc, sz; unsigned char at;
    if (!fat_resolve(base_dir, path, 0, &fc, &sz, &at) || (at & 0x10)) {
        print_string("cat: no such file\n"); return;
    }
    fat_load_geom();
    unsigned int remaining = sz;
    unsigned int cluster = fc;
    unsigned char buf[SECTOR_SIZE];
    while (remaining > 0 && cluster != 0 && cluster != 0xFFFF) {
        unsigned int base = G->data_start_sector + (cluster - 2) * G->sectors_per_cluster;
        for (unsigned int s = 0; s < G->sectors_per_cluster && remaining > 0; s++) {
            ata_read_sector(base + s, buf);
            unsigned int chunk = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            for (unsigned int i = 0; i < chunk; i++) put_char((char)buf[i]);
            remaining -= chunk;
        }
        if (remaining == 0) break;
        cluster = fat_next_cluster(cluster);
    }
    put_char('\n');
}

void fat_write_path(int base_dir, const char* path, const char* content) {
    fat_load_geom();
    char dirpath[FAT_PATH_MAX];
    char fname[64];
    int len = 0; while (path[len]) len++;
    int cut = len;
    while (cut > 0 && path[cut - 1] != '/') cut--;
    if (cut > 0) {
        int k = 0;
        for (int i = 0; i < cut - 1 && k < FAT_PATH_MAX - 1; i++) dirpath[k++] = path[i];
        dirpath[k] = 0;
        int f = 0; for (int i = cut; path[i] && f < 63; i++) fname[f++] = path[i];
        fname[f] = 0;
    } else {
        dirpath[0] = 0;
        int f = 0; for (int i = 0; path[i] && f < 63; i++) fname[f++] = path[i];
        fname[f] = 0;
    }
    unsigned int dir = (unsigned int)base_dir;
    if (dirpath[0]) {
        unsigned int fc; unsigned char at;
        if (!fat_resolve(base_dir, dirpath, &dir, &fc, 0, &at) || !(at & 0x10)) {
            print_string("write: invalid directory\n"); return;
        }
        dir = fc;
    }
    unsigned char fat_name[11];
    fat_make_name(fname, fat_name);
    unsigned int len2 = 0; while (content[len2]) len2++;
    if (len2 == 0) { print_string("write: empty content\n"); return; }
    unsigned int cluster_size = G->sectors_per_cluster * SECTOR_SIZE;
    unsigned int needed = (len2 + cluster_size - 1) / cluster_size;
    if (needed > MAX_CLUSTERS_PER_FILE) { print_string("write: file too large\n"); return; }
    unsigned int old_fc, old_sz; unsigned char old_at;
    int exists = fat_dir_lookup(dir, fat_name, 0, 0, &old_fc, &old_sz, &old_at);
    if (exists && (old_at & 0x10)) { print_string("write: name is a directory\n"); return; }
    if (exists && old_fc != 0) fat_free_chain(old_fc);
    unsigned int chain[MAX_CLUSTERS_PER_FILE];
    if (!fat_alloc_chain(needed, chain)) { print_string("write: no free space\n"); return; }
    fat_write_chain(chain, needed);
    unsigned int written = 0;
    for (unsigned int ci = 0; ci < needed; ci++) {
        unsigned int cluster = chain[ci];
        unsigned int base = G->data_start_sector + (cluster - 2) * G->sectors_per_cluster;
        for (unsigned int s = 0; s < G->sectors_per_cluster; s++) {
            unsigned char data[SECTOR_SIZE];
            for (int k = 0; k < SECTOR_SIZE; k++) data[k] = 0;
            for (int k = 0; k < SECTOR_SIZE && written < len2; k++) data[k] = content[written++];
            ata_write_sector(base + s, data);
        }
    }
    unsigned int ws; int we;
    if (exists) fat_dir_lookup(dir, fat_name, &ws, &we, 0, 0, 0);
    else if (!fat_dir_alloc_slot(dir, &ws, &we)) { print_string("write: directory full\n"); return; }
    unsigned char buf[SECTOR_SIZE];
    ata_read_sector(ws, buf);
    fat_entry_t* ent = (fat_entry_t*)&buf[we * 32];
    for (int k = 0; k < 11; k++) ent->name[k] = fat_name[k];
    ent->attr = 0x00;
    ent->first_cluster = (unsigned short)(chain[0] & 0xFFFF);
    ent->first_cluster_high = (unsigned short)((chain[0] >> 16) & 0xFFFF);
    ent->file_size = len2;
    ata_write_sector(ws, buf);
    // Успешная запись НЕ печатает: вывод в консоль (LFB) скроллил экран
    // (баг «рабочий стол улетает вверх»). Ошибки ниже — печатаются.
}

// Читает файл целиком в буфер. Возвращает размер или -1.
int fat_read_buf(int base_dir, const char* path, unsigned char* out, unsigned int max) {
    unsigned int fc, sz; unsigned char at;
    if (!fat_resolve(base_dir, path, 0, &fc, &sz, &at) || (at & 0x10)) return -1;
    if (sz > max) sz = max;
    fat_load_geom();
    unsigned int remaining = sz;
    unsigned int cluster = fc;
    unsigned int pos = 0;
    unsigned char buf[SECTOR_SIZE];
    while (remaining > 0 && cluster != 0 && cluster != 0xFFFF) {
        unsigned int base = G->data_start_sector + (cluster - 2) * G->sectors_per_cluster;
        for (unsigned int s = 0; s < G->sectors_per_cluster && remaining > 0; s++) {
            ata_read_sector(base + s, buf);
            unsigned int chunk = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            for (unsigned int i = 0; i < chunk; i++) out[pos++] = buf[i];
            remaining -= chunk;
        }
        if (remaining == 0) break;
        cluster = fat_next_cluster(cluster);
    }
    return (int)sz;
}

int fat_mkdir(int base_dir, const char* path) {
    fat_load_geom();
    char dirpath[FAT_PATH_MAX];
    char dname[64];
    int len = 0; while (path[len]) len++;
    int cut = len;
    while (cut > 0 && path[cut - 1] != '/') cut--;
    if (cut > 0) {
        int k = 0;
        for (int i = 0; i < cut - 1 && k < FAT_PATH_MAX - 1; i++) dirpath[k++] = path[i];
        dirpath[k] = 0;
        int f = 0; for (int i = cut; path[i] && f < 63; i++) dname[f++] = path[i];
        dname[f] = 0;
    } else {
        dirpath[0] = 0;
        int f = 0; for (int i = 0; path[i] && f < 63; i++) dname[f++] = path[i];
        dname[f] = 0;
    }
    unsigned int parent = (unsigned int)base_dir;
    if (dirpath[0]) {
        unsigned int fc; unsigned char at;
        if (!fat_resolve(base_dir, dirpath, &parent, &fc, 0, &at) || !(at & 0x10)) {
            print_string("mkdir: invalid parent\n"); return 0;
        }
        parent = fc;
    }
    unsigned char fat_name[11];
    fat_make_name(dname, fat_name);
    if (fat_dir_lookup(parent, fat_name, 0, 0, 0, 0, 0)) {
        print_string("mkdir: already exists\n"); return 0;
    }
    unsigned int chain[1];
    if (!fat_alloc_chain(1, chain)) { print_string("mkdir: no free space\n"); return 0; }
    fat_write_chain(chain, 1);
    unsigned char buf[SECTOR_SIZE];
    for (int k = 0; k < SECTOR_SIZE; k++) buf[k] = 0;
    fat_entry_t* dot = (fat_entry_t*)&buf[0];
    for (int k = 0; k < 11; k++) dot->name[k] = ' ';
    dot->name[0] = '.';
    dot->attr = 0x10;
    dot->first_cluster = (unsigned short)chain[0];
    fat_entry_t* dotdot = (fat_entry_t*)&buf[32];
    for (int k = 0; k < 11; k++) dotdot->name[k] = ' ';
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    dotdot->attr = 0x10;
    dotdot->first_cluster = (parent == 0) ? 0 : (unsigned short)parent;
    unsigned int mbase = G->data_start_sector + (chain[0] - 2) * G->sectors_per_cluster;
    for (unsigned int s = 0; s < G->sectors_per_cluster; s++)
        ata_write_sector(mbase + s, buf);
    unsigned int ws; int we;
    if (!fat_dir_alloc_slot(parent, &ws, &we)) { print_string("mkdir: parent full\n"); return 0; }
    ata_read_sector(ws, buf);
    fat_entry_t* ent = (fat_entry_t*)&buf[we * 32];
    for (int k = 0; k < 11; k++) ent->name[k] = fat_name[k];
    ent->attr = 0x10;
    ent->first_cluster = (unsigned short)chain[0];
    ent->first_cluster_high = 0;
    ent->file_size = 0;
    ata_write_sector(ws, buf);
    print_string("OK: created directory ");
    print_string(dname);
    print_string("\n");
    return 1;
}

int fat_cd(const char* path, unsigned int* out_cluster) {
    unsigned int dir, fc; unsigned char at;
    if (!fat_resolve(0, path, &dir, &fc, 0, &at) || !(at & 0x10)) return 0;
    *out_cluster = fc;
    return 1;
}

// ============================================================
// ФОРМАТИРОВАНИЕ FAT (создание ФС с нуля)
// ============================================================
// Параметры по умолчанию — под наши образы (512 б/сек, 4 сект/кластер,
// 2 FAT, 512 записей корня). total_sectors задаётся вызывающим (mofsf).
// Сейчас поддержан только FAT16. Возвращает 1 при успехе.
int fat_format(int version) {
    if (version != 16) return 0;   // TODO: 12/32

    ata_select(g_fat_drive);

    // Сбросить кэш геометрии для этого диска (после форматирования — перечитать).
    g_geom[g_fat_drive].valid = 0;

    unsigned int total_sectors = 65536;   // 32 МБ по умолчанию (как disk2.img)
    unsigned int spc = 4;                 // секторов на кластер
    unsigned int reserved = 256;          // зарезерв. секторов под загрузчик/ядро
    unsigned int nfats = 2;
    unsigned int root_ents = 512;

    // Размер одной FAT (секторов). Формула FAT16: spf = (total_clusters*2 + 2 + sps-1)/sps
    // Упрощённо итерируем: подбираем spf так, чтобы data_clusters влезли в FAT16.
    unsigned int spf = 0;
    for (unsigned int cand = 1; cand < 256; cand++) {
        unsigned int data_sec = total_sectors - (reserved + nfats * cand + (root_ents * 32) / SECTOR_SIZE);
        unsigned int clusters = data_sec / spc;
        unsigned int fat_capacity = (cand * SECTOR_SIZE) / 2;   // записей FAT16
        if (clusters + 2 <= fat_capacity) { spf = cand; break; }
    }
    if (spf == 0) return 0;

    // --- 1. BPB (сектор 0) ---
    unsigned char buf[SECTOR_SIZE];
    for (int k = 0; k < SECTOR_SIZE; k++) buf[k] = 0;
    fat_bpb_t* bpb = (fat_bpb_t*)buf;
    bpb->jmp[0] = 0xEB; bpb->jmp[1] = 0x3C; bpb->jmp[2] = 0x90;
    const char* oem = "MSYS2.0 ";
    for (int k = 0; k < 8; k++) bpb->oem_name[k] = (unsigned char)oem[k];
    bpb->bytes_per_sector = SECTOR_SIZE;
    bpb->sectors_per_cluster = (unsigned char)spc;
    bpb->reserved_sectors = (unsigned short)reserved;
    bpb->fat_count = (unsigned char)nfats;
    bpb->root_entry_count = (unsigned short)root_ents;
    bpb->total_sectors_16 = (total_sectors < 0x10000) ? (unsigned short)total_sectors : 0;
    bpb->media_descriptor = 0xF8;
    bpb->sectors_per_fat_16 = (unsigned short)spf;
    bpb->sectors_per_track = 32;
    bpb->head_count = 64;
    bpb->hidden_sectors = 0;
    bpb->total_sectors_32 = (total_sectors >= 0x10000) ? total_sectors : 0;
    // Сигнатура 0x55AA
    buf[510] = 0x55; buf[511] = 0xAA;
    ata_write_sector(0, buf);

    // --- 2. FAT-таблицы (2 копии). Первые 2 записи: media + EOF ---
    // FAT16: entry0 = 0xFFF8, entry1 = 0xFFFF.
    unsigned char fbuf[SECTOR_SIZE];
    for (int k = 0; k < SECTOR_SIZE; k++) fbuf[k] = 0;
    fbuf[0] = 0xF8; fbuf[1] = 0xFF;   // entry 0 = 0xFFF8
    fbuf[2] = 0xFF; fbuf[3] = 0xFF;   // entry 1 = 0xFFFF
    unsigned int fat_start = reserved;
    for (unsigned int f = 0; f < nfats; f++) {
        unsigned int base = fat_start + f * spf;
        ata_write_sector(base, fbuf);   // первый сектор FAT
        // Остальные сектора FAT — нули.
        unsigned char zb[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) zb[k] = 0;
        for (unsigned int s = 1; s < spf; s++) ata_write_sector(base + s, zb);
    }

    // --- 3. Корневой каталог (нули) ---
    unsigned int root_start = reserved + nfats * spf;
    unsigned int root_secs = (root_ents * 32) / SECTOR_SIZE;
    unsigned char eb[SECTOR_SIZE];
    for (int k = 0; k < SECTOR_SIZE; k++) eb[k] = 0;
    for (unsigned int s = 0; s < root_secs; s++) ata_write_sector(root_start + s, eb);

    // Перечитать геометрию.
    fat_load_geom();
    return 1;
}
