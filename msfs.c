// msfs.c - MSFS: Mystic Soft File System (ПРОКЛЯТОЕ ИЗДАНИЕ).
//
//   Я придумал эту ФС сам. Поэтому никто, кроме меня, не сможет
//   написать под неё драйвер с первого раза. И это МОЯ МЕСТЬ.
//
//   Правила выживания для того, кто пишет драйвер MSFS:
//     1. ИНОДЫ ЛЕЖАТ НЕ ПО ПОРЯДКУ. Логический inode i хранится в
//        физическом слоте bit_reverse(i). Удачи в отладке.
//     2. КЛАСТЕРЫ ДАННЫХ ТОЖЕ ПЕРЕМЕШАНЫ (bit_reverse 13 бит).
//     3. ИМЕНА ФАЙЛОВ ЗАШИФРОВАНЫ XOR'ом. Ключ зависит от номера inode.
//     4. КАЖДЫЙ INODE ИМЕЕТ КОНТРОЛЬНУЮ СУММУ. Не посчитаешь — не прочтёшь.
//     5. МАГИЯ НА ДИСКЕ ЗАПИСАНА ЗАДОМ НАПЕРЁД: "SFSM", не "MSFS".
//
//   Почему так? Потому что могу. Это моя ОС, моя ФС, мои правила.
//   Другие программисты должны СТРАДАТЬ. (привет, будущий я, который
//   через год забыл, как это работает.)
#include "msfs.h"
#include "ata.h"

void print_string(const char* str);
void put_char(char c);
void print_int(int num);

#define SECTOR_SIZE 512

#define MSFS_MAGIC       "MSFS"
#define MSFS_VERSION     1
#define MSFS_MAX_INODES  64
#define MSFS_INODE_SIZE  64
#define MSFS_NAME_LEN    32
#define MSFS_MAX_CHAIN   16
#define MSFS_ROOT_INODE  0
#define MSFS_TYPE_FREE   0
#define MSFS_TYPE_FILE   1
#define MSFS_TYPE_DIR    2
#define MSFS_PATH_MAX    128

// Ключ шифрования имён (зависит от номера inode).
#define MSFS_NAME_KEY(i)  ((unsigned char)(((i) * 73u + 0x5Au) & 0xFF))

// Контрольная сумма inode лежит в поле reserved (offset 33).
#define MSFS_CHK_OFF 33

typedef struct {
    char          magic[4];
    unsigned int  version;
    unsigned int  sector_size;
    unsigned int  inode_start;
    unsigned int  inode_count;
    unsigned int  data_start;
    unsigned int  cluster_size;
    unsigned int  cluster_count;
    unsigned int  next_start;
} __attribute__((packed)) msfs_super_t;

typedef struct {
    char          name[MSFS_NAME_LEN];
    unsigned char type;
    unsigned char reserved;       // тут контрольная сумма
    unsigned int  size;
    unsigned int  first_cluster;
    unsigned int  parent;
    unsigned int  next[MSFS_MAX_CHAIN];
    unsigned char pad[16];
} __attribute__((packed)) msfs_inode_t;

static msfs_super_t g_super[2];
static msfs_inode_t g_inodes[2][MSFS_MAX_INODES];
static int g_loaded[2] = {0, 0};
static int g_msfs_drive = 0;

#define S (&g_super[g_msfs_drive])
#define INODES (g_inodes[g_msfs_drive])

// ============================================================
// ПРОКЛЯТИЕ #1 и #2: перемешивание. bit-reverse — биекция,
// значит всё честно, просто выглядит как хаос.
// ============================================================
static unsigned int msfs_perm6(unsigned int i) {
    unsigned int r = 0;
    for (int b = 0; b < 6; b++) { r = (r << 1) | (i & 1); i >>= 1; }
    return r;
}
static unsigned int msfs_perm_cluster(unsigned int c) {
    unsigned int r = 0;
    for (int b = 0; b < 13; b++) { r = (r << 1) | (c & 1); c >>= 1; }
    return r;
}

// ============================================================
// ПРОКЛЯТИЕ #4: контрольная сумма inode.
// ============================================================
static unsigned char msfs_inode_chk(const unsigned char* b) {
    unsigned int s = 0;
    for (int i = 0; i < MSFS_INODE_SIZE; i++) if (i != MSFS_CHK_OFF) s += b[i];
    return (unsigned char)(s & 0xFF);
}

void msfs_use_drive(int d) {
    g_msfs_drive = d & 1;
    ata_select(g_msfs_drive);
}
int msfs_current_drive(void) { return g_msfs_drive; }

// --- helpers ---
static int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int str_eq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
static void str_cpy_n(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int msfs_read_super(void) {
    ata_select(g_msfs_drive);
    unsigned char buf[SECTOR_SIZE];
    ata_read_sector(0, buf);
    msfs_super_t* s = (msfs_super_t*)buf;
    // ПРОКЛЯТИЕ #5: магия задом наперёд.
    if (s->magic[0] != 'S' || s->magic[1] != 'F' ||
        s->magic[2] != 'S' || s->magic[3] != 'M') return 0;
    S->version       = s->version;
    S->sector_size   = s->sector_size;
    S->inode_start   = s->inode_start;
    S->inode_count   = s->inode_count;
    S->data_start    = s->data_start;
    S->cluster_size  = s->cluster_size;
    S->cluster_count = s->cluster_count;
    S->next_start    = s->next_start;
    for (int k = 0; k < 4; k++) S->magic[k] = s->magic[k];
    return 1;
}

// Загрузка всех inode с расшифровкой, проверкой суммы и снятием перемешивания.
static void msfs_load_inodes(void) {
    unsigned int total = S->inode_count;
    if (total > MSFS_MAX_INODES) total = MSFS_MAX_INODES;
    unsigned int per_sector = SECTOR_SIZE / MSFS_INODE_SIZE;
    ata_select(g_msfs_drive);
    for (unsigned int i = 0; i < total; i++) {
        unsigned int p   = msfs_perm6(i);
        unsigned int lba = S->inode_start + p / per_sector;
        unsigned int slt = p % per_sector;
        unsigned char buf[SECTOR_SIZE];
        ata_read_sector(lba, buf);
        unsigned char* src = &buf[slt * MSFS_INODE_SIZE];
        unsigned char* d   = (unsigned char*)&INODES[i];
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) d[k] = src[k];
        if (d[MSFS_CHK_OFF] != msfs_inode_chk(d)) {
            for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) d[k] = 0;
            continue;
        }
        unsigned char key = MSFS_NAME_KEY(i);
        for (int k = 0; k < MSFS_NAME_LEN; k++) d[k] ^= key;
    }
    g_loaded[g_msfs_drive] = 1;
}

// Сохранение inode: шифруем имя, считаем сумму, кладём в перемешанный слот.
static void msfs_save_inodes(void) {
    unsigned int total = S->inode_count;
    if (total > MSFS_MAX_INODES) total = MSFS_MAX_INODES;
    unsigned int per_sector = SECTOR_SIZE / MSFS_INODE_SIZE;
    ata_select(g_msfs_drive);
    for (unsigned int i = 0; i < total; i++) {
        unsigned char tmp[MSFS_INODE_SIZE];
        unsigned char* s = (unsigned char*)&INODES[i];
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) tmp[k] = s[k];
        unsigned char key = MSFS_NAME_KEY(i);
        for (int k = 0; k < MSFS_NAME_LEN; k++) tmp[k] ^= key;
        tmp[MSFS_CHK_OFF] = 0;
        tmp[MSFS_CHK_OFF] = msfs_inode_chk(tmp);
        unsigned int p   = msfs_perm6(i);
        unsigned int lba = S->inode_start + p / per_sector;
        unsigned int slt = p % per_sector;
        unsigned char buf[SECTOR_SIZE];
        ata_read_sector(lba, buf);
        unsigned char* dst = &buf[slt * MSFS_INODE_SIZE];
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) dst[k] = tmp[k];
        ata_write_sector(lba, buf);
    }
}

int msfs_probe(void) {
    if (!msfs_read_super()) return 0;
    if (S->sector_size != SECTOR_SIZE) return 0;
    if (S->inode_count == 0 || S->inode_count > MSFS_MAX_INODES) return 0;
    msfs_load_inodes();
    return 1;
}

void msfs_format(void) {
    unsigned char buf[SECTOR_SIZE];
    S->version       = MSFS_VERSION;
    S->sector_size   = SECTOR_SIZE;
    S->inode_start   = 1;
    S->inode_count   = MSFS_MAX_INODES;
    S->cluster_size  = 1;
    S->cluster_count = 8192;
    unsigned int inode_sectors = (MSFS_MAX_INODES * MSFS_INODE_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE;
    S->next_start = S->inode_start + inode_sectors;
    unsigned int next_sectors = (S->cluster_count * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    S->data_start = S->next_start + next_sectors;

    ata_select(g_msfs_drive);
    for (int k = 0; k < SECTOR_SIZE; k++) buf[k] = 0;
    msfs_super_t* s = (msfs_super_t*)buf;
    s->magic[0]='S'; s->magic[1]='F'; s->magic[2]='S'; s->magic[3]='M';
    s->version=S->version; s->sector_size=S->sector_size;
    s->inode_start=S->inode_start; s->inode_count=S->inode_count;
    s->data_start=S->data_start; s->cluster_size=S->cluster_size;
    s->cluster_count=S->cluster_count; s->next_start=S->next_start;
    ata_write_sector(0, buf);

    for (int i = 0; i < MSFS_MAX_INODES; i++) {
        unsigned char* p = (unsigned char*)&INODES[i];
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) p[k] = 0;
        INODES[i].type = MSFS_TYPE_FREE;
    }
    str_cpy_n(INODES[MSFS_ROOT_INODE].name, "/", MSFS_NAME_LEN);
    INODES[MSFS_ROOT_INODE].type = MSFS_TYPE_DIR;
    INODES[MSFS_ROOT_INODE].size = 0;
    INODES[MSFS_ROOT_INODE].first_cluster = 0xFFFFFFFF;
    INODES[MSFS_ROOT_INODE].parent = MSFS_ROOT_INODE;
    for (int i = 0; i < MSFS_MAX_CHAIN; i++) INODES[MSFS_ROOT_INODE].next[i] = 0xFFFFFFFF;
    msfs_save_inodes();

    {
        unsigned char nbuf[SECTOR_SIZE];
        for (unsigned int k = 0; k < SECTOR_SIZE; k++) nbuf[k] = 0xFF;
        unsigned int next_sectors = (S->cluster_count * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        for (unsigned int s2 = 0; s2 < next_sectors; s2++)
            ata_write_sector(S->next_start + s2, nbuf);
    }
    g_loaded[g_msfs_drive] = 1;
}

static int msfs_find_in(int parent, const char* name) {
    for (unsigned int i = 0; i < S->inode_count; i++) {
        if (INODES[i].type == MSFS_TYPE_FREE) continue;
        if ((unsigned int)INODES[i].parent != (unsigned int)parent) continue;
        if (str_eq(INODES[i].name, name)) return (int)i;
    }
    return -1;
}

static int msfs_alloc_inode(void) {
    for (unsigned int i = 1; i < S->inode_count; i++)
        if (INODES[i].type == MSFS_TYPE_FREE) return (int)i;
    return -1;
}

static int msfs_alloc_cluster(void) {
    unsigned char buf[SECTOR_SIZE];
    unsigned int idx = 0;
    unsigned int lba = S->next_start;
    for (unsigned int s = 0; s < (S->cluster_count * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE; s++) {
        ata_read_sector(lba + s, buf);
        unsigned int* n = (unsigned int*)buf;
        for (int i = 0; i < SECTOR_SIZE / 4; i++) {
            if (idx >= S->cluster_count) return -1;
            if (n[i] == 0xFFFFFFFF) {
                n[i] = 0xFFFFFFFE;
                ata_write_sector(lba + s, buf);
                return (int)idx;
            }
            idx++;
        }
    }
    return -1;
}

static void msfs_free_cluster(unsigned int cluster) {
    unsigned char buf[SECTOR_SIZE];
    unsigned int lba = S->next_start + (cluster * 4) / SECTOR_SIZE;
    unsigned int off = (cluster * 4) % SECTOR_SIZE;
    ata_read_sector(lba, buf);
    *(unsigned int*)&buf[off] = 0xFFFFFFFF;
    ata_write_sector(lba, buf);
}

static int msfs_resolve(const char* path) {
    if (path == 0 || path[0] == 0) return MSFS_ROOT_INODE;
    int cur = MSFS_ROOT_INODE;
    int i = (path[0] == '/') ? 1 : 0;
    char comp[MSFS_NAME_LEN];
    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == 0) break;
        int c = 0;
        while (path[i] && path[i] != '/' && c < MSFS_NAME_LEN - 1) comp[c++] = path[i++];
        comp[c] = 0;
        if (str_eq(comp, ".")) continue;
        if (str_eq(comp, "..")) {
            if (cur != MSFS_ROOT_INODE) cur = (int)INODES[cur].parent;
            continue;
        }
        int found = msfs_find_in(cur, comp);
        if (found < 0) return -1;
        cur = found;
    }
    return cur;
}

static void msfs_basename(const char* path, char* out) {
    int len = str_len(path);
    int start = len;
    while (start > 0 && path[start - 1] != '/') start--;
    str_cpy_n(out, path + start, MSFS_NAME_LEN);
}

static int msfs_resolve_parent(const char* path, char* out) {
    msfs_basename(path, out);
    int len = str_len(path);
    int cut = len;
    while (cut > 0 && path[cut - 1] != '/') cut--;
    if (cut > 0) cut--;
    if (cut == 0) return MSFS_ROOT_INODE;
    char parent_path[MSFS_PATH_MAX];
    int i = 0;
    while (i < cut && i < MSFS_PATH_MAX - 1) { parent_path[i] = path[i]; i++; }
    parent_path[i] = 0;
    return msfs_resolve(parent_path);
}

int msfs_find(const char* path) {
    if (!g_loaded[g_msfs_drive]) return 0;
    return msfs_resolve(path) >= 0 ? 1 : 0;
}

int msfs_is_dir_inode(int inode) {
    if (inode < 0 || (unsigned int)inode >= S->inode_count) return 0;
    return INODES[inode].type == MSFS_TYPE_DIR;
}

int msfs_read_file(const char* path, unsigned char* out, unsigned int max) {
    int inode = msfs_resolve(path);
    if (inode < 0 || INODES[inode].type != MSFS_TYPE_FILE) return -1;
    unsigned int remaining = INODES[inode].size;
    if (remaining > max) remaining = max;
    unsigned int cluster = INODES[inode].first_cluster;
    unsigned int pos = 0;
    unsigned char buf[SECTOR_SIZE];
    while (remaining > 0 && cluster != 0xFFFFFFFF) {
        // ПРОКЛЯТИЕ #2: физический сектор кластера — перемешанный.
        ata_read_sector(S->data_start + msfs_perm_cluster(cluster), buf);
        unsigned int chunk = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
        for (unsigned int i = 0; i < chunk; i++) out[pos++] = buf[i];
        remaining -= chunk;
        if (remaining == 0) break;
        unsigned int lba = S->next_start + (cluster * 4) / SECTOR_SIZE;
        unsigned int off = (cluster * 4) % SECTOR_SIZE;
        ata_read_sector(lba, buf);
        unsigned int next = *(unsigned int*)&buf[off];
        if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
        cluster = next;
    }
    return (int)pos;
}

int msfs_read_dir(const char* path, char* names, int* is_dir,
                  unsigned int* sizes, int max) {
    int dir = msfs_resolve(path);
    if (dir < 0 || INODES[dir].type != MSFS_TYPE_DIR) return -1;
    int count = 0;
    for (unsigned int i = 0; i < S->inode_count && count < max; i++) {
        if (INODES[i].type == MSFS_TYPE_FREE) continue;
        if ((int)i == dir) continue;
        if ((unsigned int)INODES[i].parent != (unsigned int)dir) continue;
        char* nm = names + count * 13;
        int p = 0;
        for (int k = 0; INODES[i].name[k] && p < 12; k++) nm[p++] = INODES[i].name[k];
        nm[p] = 0;
        is_dir[count] = (INODES[i].type == MSFS_TYPE_DIR) ? 1 : 0;
        sizes[count] = INODES[i].size;
        count++;
    }
    return count;
}

void msfs_list_print(const char* path) {
    int dir = msfs_resolve(path);
    if (dir < 0) { print_string("msfs: no such directory\n"); return; }
    if (INODES[dir].type != MSFS_TYPE_DIR) { print_string("msfs: not a directory\n"); return; }
    print_string("Directory: ");
    print_string(path && path[0] ? path : "/");
    print_string("\n------------------\n");
    int count = 0;
    for (unsigned int i = 0; i < S->inode_count; i++) {
        if (INODES[i].type == MSFS_TYPE_FREE) continue;
        if ((int)i == dir) continue;
        if ((unsigned int)INODES[i].parent != (unsigned int)dir) continue;
        print_string("  ");
        print_string(INODES[i].name);
        if (INODES[i].type == MSFS_TYPE_DIR) print_string("/");
        else {
            int nl = str_len(INODES[i].name);
            for (int k = nl; k < 20; k++) put_char(' ');
            print_int(INODES[i].size);
            print_string(" bytes");
        }
        put_char('\n');
        count++;
    }
    if (count == 0) print_string("  (empty)\n");
    print_string("------------------\n");
}

int msfs_mkdir(const char* path) {
    char name[MSFS_NAME_LEN];
    int parent = msfs_resolve_parent(path, name);
    if (parent < 0 || !msfs_is_dir_inode(parent)) return 0;
    if (msfs_find_in(parent, name) >= 0) return 0;
    int idx = msfs_alloc_inode();
    if (idx < 0) return 0;
    msfs_inode_t* n = &INODES[idx];
    unsigned char* p = (unsigned char*)n;
    for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) p[k] = 0;
    str_cpy_n(n->name, name, MSFS_NAME_LEN);
    n->type = MSFS_TYPE_DIR;
    n->size = 0;
    n->first_cluster = 0xFFFFFFFF;
    n->parent = (unsigned int)parent;
    for (int i = 0; i < MSFS_MAX_CHAIN; i++) n->next[i] = 0xFFFFFFFF;
    msfs_save_inodes();
    return 1;
}

int msfs_write(const char* path, const char* content) {
    char name[MSFS_NAME_LEN];
    int parent = msfs_resolve_parent(path, name);
    if (parent < 0 || !msfs_is_dir_inode(parent)) return 0;
    unsigned int len = (unsigned int)str_len(content);
    if (len == 0) return 0;
    int idx = msfs_find_in(parent, name);
    if (idx >= 0 && INODES[idx].type != MSFS_TYPE_FILE) return 0;
    if (idx < 0) {
        idx = msfs_alloc_inode();
        if (idx < 0) return 0;
        msfs_inode_t* n = &INODES[idx];
        unsigned char* p = (unsigned char*)n;
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) p[k] = 0;
        str_cpy_n(n->name, name, MSFS_NAME_LEN);
        n->type = MSFS_TYPE_FILE;
        n->parent = (unsigned int)parent;
        n->first_cluster = 0xFFFFFFFF;
        for (int i = 0; i < MSFS_MAX_CHAIN; i++) n->next[i] = 0xFFFFFFFF;
    } else {
        unsigned int c = INODES[idx].first_cluster;
        while (c != 0xFFFFFFFF && c != 0xFFFFFFFE) {
            unsigned char buf[SECTOR_SIZE];
            unsigned int lba = S->next_start + (c * 4) / SECTOR_SIZE;
            unsigned int off = (c * 4) % SECTOR_SIZE;
            ata_read_sector(lba, buf);
            unsigned int next = *(unsigned int*)&buf[off];
            msfs_free_cluster(c);
            if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
            c = next;
        }
        INODES[idx].first_cluster = 0xFFFFFFFF;
    }
    unsigned int written = 0, prev = 0xFFFFFFFF, first = 0xFFFFFFFF;
    while (written < len) {
        int cl = msfs_alloc_cluster();
        if (cl < 0) {
            unsigned int c = first;
            while (c != 0xFFFFFFFF && c != 0xFFFFFFFE) {
                unsigned char tmp[SECTOR_SIZE];
                unsigned int lba = S->next_start + (c * 4) / SECTOR_SIZE;
                unsigned int off = (c * 4) % SECTOR_SIZE;
                ata_read_sector(lba, tmp);
                unsigned int next = *(unsigned int*)&tmp[off];
                msfs_free_cluster(c);
                if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
                c = next;
            }
            INODES[idx].first_cluster = 0xFFFFFFFF;
            INODES[idx].size = 0;
            msfs_save_inodes();
            return 0;
        }
        unsigned int cluster = (unsigned int)cl;
        if (first == 0xFFFFFFFF) first = cluster;
        if (prev != 0xFFFFFFFF) {
            unsigned char buf[SECTOR_SIZE];
            unsigned int lba = S->next_start + (prev * 4) / SECTOR_SIZE;
            unsigned int off = (prev * 4) % SECTOR_SIZE;
            ata_read_sector(lba, buf);
            *(unsigned int*)&buf[off] = cluster;
            ata_write_sector(lba, buf);
        }
        unsigned char data[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) data[k] = 0;
        unsigned int chunk = len - written < SECTOR_SIZE ? len - written : SECTOR_SIZE;
        for (unsigned int k = 0; k < chunk; k++) data[k] = content[written + k];
        // ПРОКЛЯТИЕ #2: пишем кластер по перемешанному адресу.
        ata_write_sector(S->data_start + msfs_perm_cluster(cluster), data);
        written += chunk;
        prev = cluster;
    }
    INODES[idx].first_cluster = first;
    INODES[idx].size = len;
    msfs_save_inodes();
    return 1;
}
