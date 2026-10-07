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

// MSFS v2: этап 1 — ключ + шифр + чтение.

void print_string(const char* str);
void put_char(char c);
void print_int(int num);

#define SECTOR_SIZE      512
#define MSFS_VERSION     2
#define MSFS_KEY_SECTORS 59           // первые 59 секторов — ключ
#define MSFS_SUPER_LBA   59           // суперблок (зашифрован)
#define MSFS_MAX_INODES  64
#define MSFS_INODE_SIZE  64
#define MSFS_NAME_LEN    32
#define MSFS_MAX_CHAIN   16
#define MSFS_ROOT_INODE  0
#define MSFS_TYPE_FREE   0
#define MSFS_TYPE_FILE   1
#define MSFS_TYPE_DIR    2
#define MSFS_PATH_MAX    128

// ============================================================
// ШИФР (XTEA-подобный, 32 раунда, блок 8 байт, ключ 128 бит)
// ============================================================
typedef struct { unsigned int k[4]; } msfs_key_t;
#define XT_DELTA 0x9E3779B9u

static void xtea_encrypt_block(const msfs_key_t* key, unsigned int* v0, unsigned int* v1) {
    unsigned int v = *v0, w = *v1, sum = 0;
    for (int i = 0; i < 32; i++) {
        v += (((w << 4) ^ (w >> 5)) + w) ^ (sum + key->k[sum & 3]);
        sum += XT_DELTA;
        w += (((v << 4) ^ (v >> 5)) + v) ^ (sum + key->k[(sum >> 11) & 3]);
    }
    *v0 = v; *v1 = w;
}
static void xtea_decrypt_block(const msfs_key_t* key, unsigned int* v0, unsigned int* v1) {
    unsigned int v = *v0, w = *v1, sum = XT_DELTA * 32;
    for (int i = 0; i < 32; i++) {
        w -= (((v << 4) ^ (v >> 5)) + v) ^ (sum + key->k[(sum >> 11) & 3]);
        sum -= XT_DELTA;
        v -= (((w << 4) ^ (w >> 5)) + w) ^ (sum + key->k[sum & 3]);
    }
    *v0 = v; *v1 = w;
}
static void msfs_crypt(const msfs_key_t* key, unsigned char* buf, unsigned int len, int enc) {
    unsigned int n = len & ~7u;
    for (unsigned int i = 0; i < n; i += 8) {
        unsigned int v0 = buf[i] | (buf[i+1]<<8) | (buf[i+2]<<16) | ((unsigned int)buf[i+3]<<24);
        unsigned int v1 = buf[i+4] | (buf[i+5]<<8) | (buf[i+6]<<16) | ((unsigned int)buf[i+7]<<24);
        if (enc) xtea_encrypt_block(key, &v0, &v1);
        else     xtea_decrypt_block(key, &v0, &v1);
        buf[i+0]=(unsigned char)v0; buf[i+1]=(unsigned char)(v0>>8);
        buf[i+2]=(unsigned char)(v0>>16); buf[i+3]=(unsigned char)(v0>>24);
        buf[i+4]=(unsigned char)v1; buf[i+5]=(unsigned char)(v1>>8);
        buf[i+6]=(unsigned char)(v1>>16); buf[i+7]=(unsigned char)(v1>>24);
    }
}

// Маркеры фрагментов ключа.
static const unsigned char MSFS_START[5] = { 0x53, 0x54, 0x41, 0x52, 0x54 };
static const unsigned char MSFS_END[3]   = { 0x45, 0x4E, 0x44 };

// ============================================================
// ЖУРНАЛ (WAL, этап 2)
// ============================================================
#define JRNL_MAX_RECS   64           // макс. записей в транзакции
#define JRNL_EMPTY      0
#define JRNL_COMMITTED  1
#define JRNL_APPLIED    2

typedef struct {
    char          magic[4];          // "JRNL"
    unsigned int  generation;        // к какой генерации относится
    unsigned int  count;             // число записей
    unsigned int  state;             // JRNL_*
    unsigned int  lba_list[JRNL_MAX_RECS];   // какие LBA меняются
} __attribute__((packed)) msfs_journal_t;

// Текущая транзакция в ОЗУ (один буфер — журнал работает с текущим диском).
// ВАЖНО: НЕ в .bss (128 КБ сдвинули бы .bss за стек ядра 0x90000).
#define JRNL_DATA_ADDR 0x02C00000u
static msfs_journal_t g_jrnl[ATA_MAX_DRIVES];
static unsigned char (*g_jrnl_data)[SECTOR_SIZE] = (unsigned char (*)[SECTOR_SIZE])JRNL_DATA_ADDR;

#define J (&g_jrnl[g_msfs_drive])
#define JDATA (g_jrnl_data)

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
    // --- v2 этап 2: журнал + CoW ---
    unsigned int  journal_start;     // LBA заголовка журнала
    unsigned int  journal_sectors;   // всего секторов под журнал
    unsigned int  generation;        // растёт при каждой транзакции (CoW/снапшоты)
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

#include "ata.h"
static msfs_super_t g_super[ATA_MAX_DRIVES];
static msfs_inode_t g_inodes[ATA_MAX_DRIVES][MSFS_MAX_INODES];
static int g_loaded[ATA_MAX_DRIVES];
static int g_msfs_drive = 0;
static msfs_key_t g_key[ATA_MAX_DRIVES];

#define S (&g_super[g_msfs_drive])
#define K (&g_key[g_msfs_drive])
#define INODES (g_inodes[g_msfs_drive])

void msfs_use_drive(int d) {
    if (d < 0 || d >= ATA_MAX_DRIVES) d = 0;
    g_msfs_drive = d;
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

// Собрать мастер-ключ (16 байт) из первых 59 секторов.
// Сканируем секторы по порядку, находим блоки [START][data][END],
// склеиваем data в порядке ПОЯВЛЕНИЯ, берём первые 16 байт.
static int msfs_collect_key(msfs_key_t* key) {
    static unsigned char raw[MSFS_KEY_SECTORS * SECTOR_SIZE];
    for (int s = 0; s < MSFS_KEY_SECTORS; s++)
        ata_read_sector(s, raw + s * SECTOR_SIZE);
    unsigned char keybuf[64];
    int klen = 0;
    unsigned int total = MSFS_KEY_SECTORS * SECTOR_SIZE;
    for (unsigned int i = 0; i + 5 <= total && klen < 16; i++) {
        if (raw[i] != MSFS_START[0]) continue;
        int ok = 1;
        for (int j = 1; j < 5; j++) if (raw[i+j] != MSFS_START[j]) { ok = 0; break; }
        if (!ok) continue;
        unsigned int d = i + 5;
        for (unsigned int j = d; j + 3 <= total; j++) {
            if (raw[j] == MSFS_END[0] && raw[j+1] == MSFS_END[1] && raw[j+2] == MSFS_END[2]) {
                for (unsigned int t = d; t < j && klen < 64; t++) keybuf[klen++] = raw[t];
                i = j + 2;
                break;
            }
        }
    }
    if (klen < 16) return 0;
    for (int i = 0; i < 4; i++)
        key->k[i] = keybuf[i*4] | (keybuf[i*4+1]<<8) | (keybuf[i*4+2]<<16) | ((unsigned int)keybuf[i*4+3]<<24);
    return 1;
}

// ============================================================
// ЖУРНАЛ: транзакция
// ============================================================
// Начать транзакцию (state=EMPTY, count=0).
static void jrnl_begin(void) {
    J->magic[0]='J'; J->magic[1]='R'; J->magic[2]='N'; J->magic[3]='L';
    J->generation = 0;
    J->count = 0;
    J->state = JRNL_EMPTY;
}

// Добавить запись (сектор lba = данные data). Только в ОЗУ.
static int jrnl_add(unsigned int lba, const unsigned char* data) {
    if (J->count >= JRNL_MAX_RECS) return 0;
    unsigned int i = J->count;
    J->lba_list[i] = lba;
    for (int k = 0; k < SECTOR_SIZE; k++) JDATA[i][k] = data[k];
    J->count++;
    return 1;
}

// Записать заголовок журнала на диск (со всеми записями). Зашифрован.
static void jrnl_commit(unsigned int generation) {
    J->generation = generation;
    J->state = JRNL_COMMITTED;
    // Заголовок.
    unsigned char hb[SECTOR_SIZE];
    for (int k = 0; k < SECTOR_SIZE; k++) hb[k] = 0;
    msfs_journal_t* h = (msfs_journal_t*)hb;
    for (int k = 0; k < 4; k++) h->magic[k] = J->magic[k];
    h->generation = J->generation; h->count = J->count; h->state = J->state;
    for (unsigned int i = 0; i < J->count; i++) h->lba_list[i] = J->lba_list[i];
    msfs_crypt(K, hb, SECTOR_SIZE, 1);
    ata_write_sector(S->journal_start, hb);
    // Записи (по сектору на запись).
    for (unsigned int i = 0; i < J->count; i++) {
        unsigned char db[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) db[k] = JDATA[i][k];
        msfs_crypt(K, db, SECTOR_SIZE, 1);
        ata_write_sector(S->journal_start + 1 + i, db);
    }
}

// Применить транзакцию (записать данные по их LBA).
static void jrnl_apply(void) {
    for (unsigned int i = 0; i < J->count; i++) {
        unsigned char db[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) db[k] = JDATA[i][k];
        ata_write_sector(J->lba_list[i], db);
    }
    J->state = JRNL_APPLIED;
    // Сбросить заголовок в EMPTY.
    unsigned char hb[SECTOR_SIZE];
    for (int k = 0; k < SECTOR_SIZE; k++) hb[k] = 0;
    msfs_journal_t* h = (msfs_journal_t*)hb;
    h->magic[0]='J'; h->magic[1]='R'; h->magic[2]='N'; h->magic[3]='L';
    h->state = JRNL_EMPTY; h->count = 0; h->generation = J->generation;
    msfs_crypt(K, hb, SECTOR_SIZE, 1);
    ata_write_sector(S->journal_start, hb);
}

// Recovery: если на диске state==COMMITTED, докатить.
static void jrnl_recovery(void) {
    unsigned char hb[SECTOR_SIZE];
    ata_read_sector(S->journal_start, hb);
    msfs_crypt(K, hb, SECTOR_SIZE, 0);
    msfs_journal_t* h = (msfs_journal_t*)hb;
    if (h->magic[0]!='J'||h->magic[1]!='R'||h->magic[2]!='N'||h->magic[3]!='L') return;
    if (h->state != JRNL_COMMITTED) return;
    // Прочитать записи.
    unsigned int n = h->count;
    if (n > JRNL_MAX_RECS) n = JRNL_MAX_RECS;
    for (unsigned int i = 0; i < n; i++) {
        unsigned char db[SECTOR_SIZE];
        ata_read_sector(S->journal_start + 1 + i, db);
        msfs_crypt(K, db, SECTOR_SIZE, 0);
        for (int k = 0; k < SECTOR_SIZE; k++) JDATA[i][k] = db[k];
        J->lba_list[i] = h->lba_list[i];
    }
    J->count = n; J->state = JRNL_COMMITTED;
    jrnl_apply();   // докатить
}

static int msfs_read_super(void) {
    ata_select(g_msfs_drive);
    unsigned char buf[SECTOR_SIZE];
    ata_read_sector(MSFS_SUPER_LBA, buf);
    msfs_crypt(K, buf, SECTOR_SIZE, 0);
    msfs_super_t* s = (msfs_super_t*)buf;
    if (s->magic[0]!='M'||s->magic[1]!='F'||s->magic[2]!='S'||s->magic[3]!='2') return 0;
    S->version       = s->version;
    S->sector_size   = s->sector_size;
    S->inode_start   = s->inode_start;
    S->inode_count   = s->inode_count;
    S->data_start    = s->data_start;
    S->cluster_size  = s->cluster_size;
    S->cluster_count = s->cluster_count;
    S->next_start    = s->next_start;
    S->journal_start   = s->journal_start;
    S->journal_sectors = s->journal_sectors;
    S->generation      = s->generation;
    for (int k = 0; k < 4; k++) S->magic[k] = s->magic[k];
    return 1;
}

// Загрузка inode (расшифровка сектора).
static void msfs_load_inodes(void) {
    unsigned int total = S->inode_count;
    if (total > MSFS_MAX_INODES) total = MSFS_MAX_INODES;
    unsigned int per_sector = SECTOR_SIZE / MSFS_INODE_SIZE;
    ata_select(g_msfs_drive);
    for (unsigned int i = 0; i < total; i++) {
        unsigned int lba = S->inode_start + i / per_sector;
        unsigned int slt = i % per_sector;
        unsigned char buf[SECTOR_SIZE];
        ata_read_sector(lba, buf);
        msfs_crypt(K, buf, SECTOR_SIZE, 0);
        unsigned char* src = &buf[slt * MSFS_INODE_SIZE];
        unsigned char* d   = (unsigned char*)&INODES[i];
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) d[k] = src[k];
    }
    g_loaded[g_msfs_drive] = 1;
}

// Сохранение inode ЧЕРЕЗ ЖУРНАЛ (WAL).
static void msfs_save_inodes(void) {
    unsigned int total = S->inode_count;
    if (total > MSFS_MAX_INODES) total = MSFS_MAX_INODES;
    unsigned int per_sector = SECTOR_SIZE / MSFS_INODE_SIZE;
    ata_select(g_msfs_drive);
    jrnl_begin();
    for (unsigned int s = 0; s * per_sector < total; s++) {
        unsigned int lba = S->inode_start + s;
        unsigned char buf[SECTOR_SIZE];
        ata_read_sector(lba, buf);
        msfs_crypt(K, buf, SECTOR_SIZE, 0);
        for (unsigned int slt = 0; slt < per_sector; slt++) {
            unsigned int i = s * per_sector + slt;
            if (i >= total) break;
            unsigned char* src = (unsigned char*)&INODES[i];
            unsigned char* dst = &buf[slt * MSFS_INODE_SIZE];
            for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) dst[k] = src[k];
        }
        msfs_crypt(K, buf, SECTOR_SIZE, 1);
        jrnl_add(lba, buf);
    }
    jrnl_commit(S->generation);
    jrnl_apply();
}

int msfs_probe(void) {
    if (!msfs_collect_key(K)) return 0;   // без ключа ничего не прочесть
    if (!msfs_read_super()) return 0;
    if (S->sector_size != SECTOR_SIZE) return 0;
    if (S->inode_count == 0 || S->inode_count > MSFS_MAX_INODES) return 0;
    jrnl_recovery();   // докатить незавершённую транзакцию
    msfs_load_inodes();
    return 1;
}

void msfs_format(void) {
    // Этап 1: ключ фиксированный (16 байт), разложен фрагментами по 4 байта
    // в секторы 37,5,51,22 (псевдослучайный порядок) в виде [START][4 байта][END].
    // TODO (этап 4): генерация случайного ключа.
    static const unsigned char MASTER[16] = {
        0x4D,0x79,0x73,0x74,0x69,0x63,0x4F,0x53, 0x32,0x4B,0x65,0x79,0x21,0x21,0x21,0x21
    };
    static const unsigned char FRAG_SEC[4] = { 37, 5, 51, 22 };

    ata_select(g_msfs_drive);

    // 1. Сначала ЗАНУЛИТЬ все ключевые секторы, потом разложить фрагменты.
    for (int s = 0; s < MSFS_KEY_SECTORS; s++) {
        unsigned char zb[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) zb[k] = 0;
        ata_write_sector(s, zb);
    }
    for (int f = 0; f < 4; f++) {
        unsigned char kb[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) kb[k] = 0;
        int p = 0;
        for (int j = 0; j < 5; j++) kb[p++] = MSFS_START[j];
        for (int j = 0; j < 4; j++) kb[p++] = MASTER[f*4 + j];
        for (int j = 0; j < 3; j++) kb[p++] = MSFS_END[j];
        ata_write_sector(FRAG_SEC[f], kb);
    }

    // 2. Ключ для шифрования — собираем ТЕМ ЖЕ способом, что probe
    //    (по порядку появления START на диске), иначе ключи не совпадут.
    msfs_collect_key(K);

    // 3. Геометрия.
    S->version       = MSFS_VERSION;
    S->sector_size   = SECTOR_SIZE;
    S->inode_start   = MSFS_SUPER_LBA + 1;   // 60
    S->inode_count   = MSFS_MAX_INODES;
    S->cluster_size  = 1;
    S->cluster_count = 8192;
    unsigned int inode_sectors = (MSFS_MAX_INODES * MSFS_INODE_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE;
    S->next_start = S->inode_start + inode_sectors;
    unsigned int next_sectors = (S->cluster_count * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    S->journal_start   = S->next_start + next_sectors;
    S->journal_sectors = 1 + JRNL_MAX_RECS;   // заголовок + записи
    S->data_start      = S->journal_start + S->journal_sectors;
    S->generation      = 0;

    // Записать суперблок (зашифрован).
    {
        unsigned char buf[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) buf[k] = 0;
        msfs_super_t* s = (msfs_super_t*)buf;
        s->magic[0]='M'; s->magic[1]='F'; s->magic[2]='S'; s->magic[3]='2';
        s->version=S->version; s->sector_size=S->sector_size;
        s->inode_start=S->inode_start; s->inode_count=S->inode_count;
        s->data_start=S->data_start; s->cluster_size=S->cluster_size;
        s->cluster_count=S->cluster_count; s->next_start=S->next_start;
        s->journal_start=S->journal_start; s->journal_sectors=S->journal_sectors;
        s->generation=S->generation;
        msfs_crypt(K, buf, SECTOR_SIZE, 1);
        ata_write_sector(MSFS_SUPER_LBA, buf);
    }

    // Журнал: пустой заголовок (EMPTY).
    jrnl_begin();
    {
        unsigned char hb[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) hb[k] = 0;
        msfs_journal_t* h = (msfs_journal_t*)hb;
        h->magic[0]='J'; h->magic[1]='R'; h->magic[2]='N'; h->magic[3]='L';
        h->state = JRNL_EMPTY; h->count = 0; h->generation = 0;
        msfs_crypt(K, hb, SECTOR_SIZE, 1);
        ata_write_sector(S->journal_start, hb);
    }

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
        // Кластер зашифрован — читаем и расшифровываем.
        ata_read_sector(S->data_start + cluster, buf);
        msfs_crypt(K, buf, SECTOR_SIZE, 0);
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

int msfs_size(const char* path) {
    int inode = msfs_resolve(path);
    if (inode < 0 || INODES[inode].type != MSFS_TYPE_FILE) return -1;
    return (int)INODES[inode].size;
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
        // Кластер шифруется при записи.
        unsigned char enc[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) enc[k] = data[k];
        msfs_crypt(K, enc, SECTOR_SIZE, 1);
        ata_write_sector(S->data_start + cluster, enc);
        written += chunk;
        prev = cluster;
    }
    INODES[idx].first_cluster = first;
    INODES[idx].size = len;
    msfs_save_inodes();
    return 1;
}
