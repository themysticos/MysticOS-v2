// vfs.c - слой виртуальной ФС MysticOS 2.
//
// Адаптировано из MysticOS 1: взяты vfs_detect (определение ФС по сигнатуре)
// и path_normalize ('.', '..', '//'). Убраны MSFS и cwd; вместо if-цепочек —
// таблица драйверов (fs_driver_t), чтобы новые ФС добавлялись регистрацией,
// а не правкой каждой функции.
#include "fs.h"
#include "fat.h"

// --- Прототипы FAT (fat.c) в терминах VFS ---
int  fat_find(int base_dir, const char* path);
int  fat_read_dir(int base_dir, const char* path, char* names, int* is_dir,
                  unsigned int* sizes, int max);
int  fat_read_file_buf(const char* path, unsigned char* buf, unsigned int max);
int  fat_mkdir(int base_dir, const char* path);
void fat_write_path(int base_dir, const char* path, const char* content);
void fat_list(int base_dir, const char* path);

extern void ata_read_sector(unsigned int lba, unsigned char* buffer);

// ============================================================
// Состояние
// ============================================================
#define VFS_MAX_DRIVERS 4
static const fs_driver_t* g_drivers[VFS_MAX_DRIVERS];
static int g_driver_count = 0;
static const fs_driver_t* g_active = 0;   // активный драйвер

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

static const fs_driver_t FAT16_DRIVER = {
    "fat16", FS_FAT16, fat_probe,
    fat_drv_find, fat_drv_read_dir, fat_drv_read_file,
    fat_drv_size, fat_drv_mkdir, fat_drv_write
};

// ============================================================
// Инициализация: выбрать активный драйвер
// ============================================================
void vfs_init(void) {
    g_driver_count = 0;
    g_active = 0;
    vfs_register(&FAT16_DRIVER);
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i]->probe && g_drivers[i]->probe()) {
            g_active = g_drivers[i];
            break;
        }
    }
}

fs_type_t vfs_get_type(void) {
    return g_active ? g_active->type : FS_UNKNOWN;
}

const char* vfs_type_name(void) {
    return g_active ? g_active->name : "Unknown";
}

// ============================================================
// Обёртки операций
// ============================================================
// Разбор префикса диска "D0:"/"D1:" и нормализация остатка.
void vfs_resolve_drive(const char* path, int* drive, char* out) {
    int d = 0;
    const char* p = path ? path : "";
    if (p[0] == 'D' && p[1] >= '0' && p[1] <= '9' && p[2] == ':') {
        d = p[1] - '0';
        p += 3;                     // пропускаем "Dx:"
        if (p[0] != '/') { out[0] = '/'; vfs_normalize(p, out + 1); }
        else vfs_normalize(p, out);
    } else {
        vfs_normalize(p, out);
    }
    if (drive) *drive = d;
}

int vfs_find(const char* path) {
    if (!g_active) return 0;
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    fat_use_drive(d);
    return g_active->find ? g_active->find(clean) : 0;
}

int vfs_read_dir(const char* path, char* names, int* is_dir,
                 unsigned int* sizes, int max) {
    if (!g_active) return -1;
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    fat_use_drive(d);
    return g_active->read_dir ? g_active->read_dir(clean, names, is_dir, sizes, max) : -1;
}

int vfs_read_file(const char* path, unsigned char* buf, unsigned int max) {
    if (!g_active) return -1;
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    fat_use_drive(d);
    return g_active->read_file ? g_active->read_file(clean, buf, max) : -1;
}

int vfs_mkdir(const char* path) {
    if (!g_active) return 0;
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    fat_use_drive(d);
    return g_active->mkdir ? g_active->mkdir(clean) : 0;
}

int vfs_write(const char* path, const char* content) {
    if (!g_active) return 0;
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    fat_use_drive(d);
    return g_active->write ? g_active->write(clean, content) : 0;
}

// Размер файла: реализуем через драйвер, если он умеет; для FAT — через
// перечисление каталога (как делал api_fs_size в kernel.c).
int vfs_size(const char* path) {
    if (!g_active) return -1;
    if (g_active->size && g_active->type != FS_FAT16) return g_active->size(path);
    // FAT: перечисляем корень и ищем запись (совместимо со старой api_fs_size).
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    fat_use_drive(d);
    char names[64 * 13]; int isdir[64]; unsigned int sizes[64];
    int n = vfs_read_dir("/", names, isdir, sizes, 64);
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
    if (!g_active) { return; }
    int d; char clean[FS_PATH_MAX];
    vfs_resolve_drive(path, &d, clean);
    fat_use_drive(d);
    if (g_active->type == FS_FAT16 || g_active->type == FS_FAT12) {
        fat_list(0, clean);
    }
}
