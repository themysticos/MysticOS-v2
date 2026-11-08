// vfs.c - слой виртуальной ФС MysticOS 2.
//
// Адаптировано из MysticOS 1: взяты vfs_detect (определение ФС по сигнатуре)
// и path_normalize ('.', '..', '//'). Убраны MSFS и cwd; вместо if-цепочек —
// таблица драйверов (fs_driver_t), чтобы новые ФС добавлялись регистрацией,
// а не правкой каждой функции.
#include "fs.h"
#include "fat.h"
#include "msfs.h"

// --- Прототипы FAT (fat.c) в терминах VFS ---
int  fat_find(int base_dir, const char* path);
int  fat_read_dir(int base_dir, const char* path, char* names, int* is_dir,
                  unsigned int* sizes, int max);
int  fat_read_file_buf(const char* path, unsigned char* buf, unsigned int max);
int  fat_mkdir(int base_dir, const char* path);
void fat_write_path(int base_dir, const char* path, const char* content);
void fat_list(int base_dir, const char* path);

void ata_select(int drive);
extern void ata_read_sector(unsigned int lba, unsigned char* buffer);

// ============================================================
// Состояние: какая ФС на каждом диске (0/1)
// ============================================================
#define VFS_MAX_DRIVERS 4
static const fs_driver_t* g_drivers[VFS_MAX_DRIVERS];
static int g_driver_count = 0;
// g_drive_fs[d] — драйвер, определённый на диске d (или 0).
// Дисков до ATA_MAX_DRIVES (4). Неподключённые — 0.
#include "ata.h"
static const fs_driver_t* g_drive_fs[ATA_MAX_DRIVES];

int vfs_register(const fs_driver_t* drv) {
    if (!drv || g_driver_count >= VFS_MAX_DRIVERS) return 0;
    g_drivers[g_driver_count++] = drv;
    return 1;
}

// ============================================================
// Драйвер FAT16 (обёртки над fat.c)
// ============================================================
static int fat_probe(void) {
    unsigned char sector[512];
    ata_read_sector(0, sector);
    // bytes_per_sector == 512 (little-endian) по смещению 11 => похоже на FAT.
    unsigned short bps = sector[11] | (sector[12] << 8);
    return (bps == 512) ? 1 : 0;
}
static int fat_drv_find(const char* path)            { return fat_find(0, path); }
static int fat_drv_read_dir(const char* path, char* names, int* is_dir,
                            unsigned int* sizes, int max) {
    return fat_read_dir(0, path, names, is_dir, sizes, max);
}
static int fat_drv_read_file(const char* path, unsigned char* buf, unsigned int max) {
    return fat_read_file_buf(path, buf, max);
}
static int fat_drv_size(const char* path) {
    // Размер через список корня+подкаталогов неудобен; используем find + чтение
    // заголовка каталога. Простейший путь: vfs_size реализуем отдельно ниже.
    (void)path; return -1;
}
static int fat_drv_mkdir(const char* path)           { return fat_mkdir(0, path); }
static void fat_drv_write_impl(const char* path, const char* content) {
    fat_write_path(0, path, content);
}
// write имеет сигнатуру int; fat_write_path возвращает void — оборачиваем.
static int fat_drv_write(const char* path, const char* content) {
    fat_drv_write_impl(path, content);
    return 1;
}
static int fat_drv_write_bin(const char* path, const unsigned char* content, unsigned int len) {
    return fat_write_path_bin(0, path, content, len);
}
// Файловый дескриптор — прямо в fat.c.
static int  fat_drv_fopen(const char* path, void* fh)         { return fat_file_open(path, fh); }
static int  fat_drv_fread(void* fh, unsigned char* buf, unsigned int n) { return fat_file_read(fh, buf, n); }
static int  fat_drv_fseek(void* fh, unsigned int pos)         { return fat_file_seek(fh, pos); }
static void fat_drv_fclose(void* fh)                          { fat_file_close(fh); }

static const fs_driver_t FAT16_DRIVER = {
    "fat16", FS_FAT16, fat_probe,
    fat_drv_find, fat_drv_read_dir, fat_drv_read_file,
    fat_drv_size, fat_drv_mkdir, fat_drv_write, fat_drv_write_bin,
    fat_drv_fopen, fat_drv_fread, fat_drv_fseek, fat_drv_fclose
};

// ============================================================
// Драйвер MSFS (обёртки над msfs.c)
// ============================================================
static int msfs_probe_drv(void) { return msfs_probe(); }
static int msfs_drv_find(const char* path)               { return msfs_find(path); }
static int msfs_drv_read_dir(const char* path, char* names, int* is_dir,
                             unsigned int* sizes, int max) {
    return msfs_read_dir(path, names, is_dir, sizes, max);
}
static int msfs_drv_read_file(const char* path, unsigned char* buf, unsigned int max) {
    return msfs_read_file(path, buf, max);
}
static int msfs_drv_size(const char* path) { return msfs_size(path); }
static int msfs_drv_mkdir(const char* path)              { return msfs_mkdir(path); }
static int msfs_drv_write(const char* path, const char* content) {
    return msfs_write(path, content);
}

static const fs_driver_t MSFS_DRIVER = {
    "msfs", FS_MSFS, msfs_probe_drv,
    msfs_drv_find, msfs_drv_read_dir, msfs_drv_read_file,
    msfs_drv_size, msfs_drv_mkdir, msfs_drv_write, 0,
    0, 0, 0, 0   // write_bin и файловый дескриптор пока не поддержаны
};

// ============================================================
// Инициализация: определить ФС на КАЖДОМ диске
// ============================================================
void vfs_init(void) {
    g_driver_count = 0;
    vfs_register(&FAT16_DRIVER);
    vfs_register(&MSFS_DRIVER);
    // Определить все подключённые ATA-устройства, затем ФС на каждом.
    for (int d = 0; d < ATA_MAX_DRIVES; d++) g_drive_fs[d] = 0;
    ata_probe_all();
    for (int d = 0; d < ATA_MAX_DRIVES; d++) {
        if (!ata_present(d)) continue;
        ata_select(d);
        fat_use_drive(d);
        msfs_use_drive(d);
        for (int i = 0; i < g_driver_count; i++) {
            if (g_drivers[i]->probe && g_drivers[i]->probe()) {
                g_drive_fs[d] = g_drivers[i];
                break;
            }
        }
    }
    // Вернуть текущий диск 0.
    ata_select(0); fat_use_drive(0); msfs_use_drive(0);
}

// Драйвер для диска d (0/1).
static const fs_driver_t* drv_for(int d) {
    if (d < 0 || d >= ATA_MAX_DRIVES) d = 0;
    return g_drive_fs[d];
}

int vfs_drive_has_fs(int d) {
    if (d < 0 || d >= ATA_MAX_DRIVES) return 0;
    return g_drive_fs[d] != 0;
}

fs_type_t vfs_get_type(void) {
    const fs_driver_t* drv = drv_for(0);
    return drv ? drv->type : FS_UNKNOWN;
}

const char* vfs_type_name(void) {
    const fs_driver_t* drv = drv_for(0);
    return drv ? drv->name : "Unknown";
}

// ============================================================
// Обёртки операций
// ============================================================
// Разбор префикса диска "D0:"/"D1:" и нормализация остатка.
void vfs_resolve_drive(const char* path, int* drive, char* out) {
    int d = 0;
    const char* p = path ? path : "";
    // Префикс диска: "D<n>:" (напр. "D0:"). Остаток — путь; допустимы
    // и "D0://foo/bar" (формат Dx://), и "D0:/foo/bar".
    if (p[0] == 'D' && p[1] >= '0' && p[1] <= '9' && p[2] == ':') {
        d = p[1] - '0';
        p += 3;                     // пропускаем "Dx:"
    }
    // p теперь начинается с '/' или '//' (или пуст/относителен).
    // vfs_normalize принимает ведущие слэши в любом количестве и сам
    // приводит путь к каноническому виду ('/dir/file').
    vfs_normalize(p, out);
    if (drive) *drive = d;
}

// Подготовить диск d для работы его драйвера.
static void vfs_switch(int d) {
    ata_select(d);
    fat_use_drive(d);
    msfs_use_drive(d);
}

int vfs_find(const char* path) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv) return 0;
    vfs_switch(d);
    return drv->find ? drv->find(clean) : 0;
}

int vfs_read_dir(const char* path, char* names, int* is_dir,
                 unsigned int* sizes, int max) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv) return -1;
    vfs_switch(d);
    return drv->read_dir ? drv->read_dir(clean, names, is_dir, sizes, max) : -1;
}

int vfs_read_file(const char* path, unsigned char* buf, unsigned int max) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv) return -1;
    vfs_switch(d);
    return drv->read_file ? drv->read_file(clean, buf, max) : -1;
}

int vfs_mkdir(const char* path) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv) return 0;
    vfs_switch(d);
    return drv->mkdir ? drv->mkdir(clean) : 0;
}

int vfs_write(const char* path, const char* content) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv) return 0;
    vfs_switch(d);
    return drv->write ? drv->write(clean, content) : 0;
}

// Бинарная запись: len явно, внутри могут быть нули (сейвы DOOM). 1/0.
int vfs_write_bin(const char* path, const unsigned char* content, unsigned int len) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv || !drv->write_bin) return 0;
    vfs_switch(d);
    return drv->write_bin(clean, content, len);
}

// ============================================================
// Файловый дескриптор (стриминг). Пул открытых файлов.
// ============================================================
#define VFS_MAX_FILES 8
static fs_file_t g_files[VFS_MAX_FILES];

int vfs_file_open(const char* path) {
    // Найти свободный слот.
    int fd = -1;
    for (int i = 0; i < VFS_MAX_FILES; i++) if (!g_files[i].used) { fd = i; break; }
    if (fd < 0) return -1;
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv || !drv->file_open) return -1;
    vfs_switch(d);
    fs_file_t* f = &g_files[fd];
    f->drive = d;
    f->pos = 0;
    int k = 0; while (clean[k] && k < FS_PATH_MAX - 1) { f->path[k] = clean[k]; k++; }
    f->path[k] = 0;
    if (!drv->file_open(clean, f->fh)) { f->used = 0; return -1; }
    f->size = (unsigned int)vfs_size(path);
    if ((int)f->size < 0) f->size = 0;
    f->used = 1;
    return fd;
}

int vfs_file_read(int fd, unsigned char* buf, unsigned int n) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].used) return -1;
    fs_file_t* f = &g_files[fd];
    const fs_driver_t* drv = drv_for(f->drive);
    if (!drv || !drv->file_read) return -1;
    vfs_switch(f->drive);
    int r = drv->file_read(f->fh, buf, n);
    if (r > 0) f->pos += (unsigned int)r;
    return r;
}

int vfs_file_seek(int fd, unsigned int pos) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].used) return -1;
    fs_file_t* f = &g_files[fd];
    const fs_driver_t* drv = drv_for(f->drive);
    if (!drv || !drv->file_seek) return -1;
    vfs_switch(f->drive);
    int r = drv->file_seek(f->fh, pos);
    if (r >= 0) f->pos = (unsigned int)r;
    return r;
}

void vfs_file_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].used) return;
    fs_file_t* f = &g_files[fd];
    const fs_driver_t* drv = drv_for(f->drive);
    if (drv && drv->file_close) { vfs_switch(f->drive); drv->file_close(f->fh); }
    f->used = 0;
}

int vfs_file_size(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].used) return -1;
    return (int)g_files[fd].size;
}

// Размер файла: реализуем через драйвер, если он умеет; для FAT — через
// перечисление каталога (как делал api_fs_size в kernel.c).
int vfs_size(const char* path) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    // Если драйвер умеет size (MSFS — нет, вернёт -1) — используем.
    const fs_driver_t* drv = drv_for(d);
    if (drv && drv->size && drv->type != FS_FAT16) {
        vfs_switch(d);
        return drv->size(clean);
    }
    // FAT: перечисляем корень и ищем запись.
    // ВАЖНО: зовём драйвер напрямую для диска d — vfs_read_dir("/") без
    // префикса сбросил бы диск на 0 (vfs_resolve_drive считает "/" диском 0).
    vfs_switch(d);
    char names[64 * 13]; int isdir[64]; unsigned int sizes[64];
    int n = (drv && drv->read_dir) ? drv->read_dir("/", names, isdir, sizes, 64) : -1;
    if (n <= 0) return -1;
    const char* b = clean;
    if (b[0] == '/') b++;
    for (int i = 0; i < n; i++) {
        const char* a = names + i * 13;
        const char* bb = b;
        while (*a && *bb && *a == *bb) { a++; bb++; }
        if (*a == 0 && *bb == 0) return isdir[i] ? -1 : (int)sizes[i];
    }
    return -1;
}

// ============================================================
// Нормализация пути (взято из MysticOS 1 vfs.c).
// Разворачивает '.', '..', '//', убирает хвостовой слэш.
// ============================================================
void vfs_normalize(const char* path, char* out) {
    char tmp[FS_PATH_MAX];
    int t = 0;
    if (path[0] == '/') {
        tmp[t++] = '/';
    } else {
        tmp[t++] = '/';   // в M2 нет cwd — относительный путь считаем от корня
    }
    tmp[t] = 0;

    int p = 0;
    while (path[p]) {
        if (path[p] == '/') { p++; continue; }
        char comp[FS_PATH_MAX];
        int c = 0;
        while (path[p] && path[p] != '/' && c < FS_PATH_MAX - 1) comp[c++] = path[p++];
        comp[c] = 0;

        if (comp[0] == '.' && comp[1] == 0) continue;
        if (comp[0] == '.' && comp[1] == '.' && comp[2] == 0) {
            int tt = 0; while (tmp[tt]) tt++;
            if (tt > 1) {
                tt--;
                while (tt > 1 && tmp[tt - 1] != '/') tt--;
                tmp[tt] = 0;
            }
            continue;
        }
        int tt = 0; while (tmp[tt]) tt++;
        if (tt > 0 && tmp[tt - 1] != '/') tmp[tt++] = '/';
        int k = 0;
        while (comp[k] && tt < FS_PATH_MAX - 1) tmp[tt++] = comp[k++];
        tmp[tt] = 0;
    }

    int tt = 0;
    while (tmp[tt] && tt < FS_PATH_MAX - 1) { out[tt] = tmp[tt]; tt++; }
    if (tt == 0) { out[0] = '/'; tt = 1; }
    out[tt] = 0;
    // Схлопываем ведущие двойные слэши: //foo -> /foo
    if (out[0] == '/' && out[1] == '/') {
        int src = 1, dst = 1;
        while (out[src] == '/') src++;
        while (out[src]) out[dst++] = out[src++];
        out[dst] = 0;
    }
    tt = 0; while (out[tt]) tt++;
    if (tt > 1 && out[tt - 1] == '/') out[tt - 1] = 0;
}

// Печать каталога (для терминала). Сейчас делегируем FAT; при появлении
// других ФС — расширим vtable полем list.
void vfs_list_print(const char* path) {
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    const fs_driver_t* drv = drv_for(d);
    if (!drv) return;
    vfs_switch(d);
    if (drv->type == FS_FAT16 || drv->type == FS_FAT12) {
        fat_list(0, clean);
    } else if (drv->type == FS_MSFS) {
        msfs_list_print(clean);
    }
}
