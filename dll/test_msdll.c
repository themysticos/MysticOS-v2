// test_msdll.c - тестовый стенд для msdll.c (собирается ОБЫЧНЫМ gcc на хосте).
//
// НЕ ЧАСТЬ ЯДРА. Просто проверка логики: парсер, релокации, resolve.
//
// Собирается (см. dll/build_test.sh):
//   gcc -m32 -ffreestanding -nostdlib -fno-builtin -fno-pic
//       -fno-stack-protector -no-pie -O2 -Wall -Wextra
//       test_msdll.c msdll.c -o test_msdll
//
// Использование:
//   В линуксе: ./test_msdll
//   В WSL:     wsl bash -c 'cd /mnt/c/.../dll && ./test_msdll'

#include "msdll.h"

// ============================================================
// Минимальный вывод (без libc)
// ============================================================
// Используем syscall write(2) напрямую — работает и в Linux, и в WSL.
// Функции обозначены как "naked" только для ясности, тут обычные.
static int sys_write(int fd, const void* buf, unsigned int n) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(4), "b"(fd), "c"(buf), "d"(n)
        : "memory"
    );
    return ret;
}

static void print(const char* s) {
    unsigned int n = 0;
    while (s[n]) n++;
    sys_write(1, s, n);
}

static void print_int(int v) {
    if (v == 0) { sys_write(1, "0", 1); return; }
    char b[12]; int i = 0; int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    if (neg) b[i++] = '-';
    char out[12]; int j = 0;
    while (i) out[j++] = b[--i];
    sys_write(1, out, (unsigned int)j);
}

static void print_hex(unsigned int v) {
    const char* h = "0123456789ABCDEF";
    char b[10]; b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 8; i++) b[2 + i] = h[(v >> (28 - i * 4)) & 0xF];
    sys_write(1, b, 10);
}

// ============================================================
// Простой printf-подобный
// ============================================================
static void println(const char* s) { print(s); print("\n"); }

// ============================================================
// Сборка фейкового .msdll в памяти
// ============================================================
// Раскладка фиктивного образа:
//   0x0000  header (64 байта)
//   0x0040  export table (1 запись)
//   0x0048  import table (1 запись)
//   0x0054  reloc table (1 запись)
//   0x005C  section table (0 записей — не нужна)
//   0x0060  строка "fake_func"
//   0x006A  строка "mskernel"
//   0x0074  строка "fake_sym"
//   0x0080  "fake_func" (тело функции: ret)
//   0x0084  RELOC TARGET: u32 для релокации
//   0x0088  RELOC SOURCE: u32, который станет base после релокации
//
// Реальная DLL будет больше и сложнее, но для теста парсера и релокаций хватит.
#define IMG_SIZE  0x200
static unsigned char g_image[IMG_SIZE];

static void put_str(unsigned int off, const char* s) {
    while (*s) g_image[off++] = (unsigned char)*s++;
    g_image[off] = 0;
}

static void put_u32(unsigned int off, unsigned int v) {
    g_image[off + 0] = (unsigned char)(v & 0xFF);
    g_image[off + 1] = (unsigned char)((v >> 8) & 0xFF);
    g_image[off + 2] = (unsigned char)((v >> 16) & 0xFF);
    g_image[off + 3] = (unsigned char)((v >> 24) & 0xFF);
}

// Строим образ. base — куда его потом "загрузим" (identity: buf == base).
// Для теста используем отдельный буфер на стеке/в .bss и base == его адрес.
static void build_image(void) {
    for (unsigned int i = 0; i < IMG_SIZE; i++) g_image[i] = 0;

    msdll_header_t* h = (msdll_header_t*)g_image;
    h->magic          = MSDLL_MAGIC;
    h->version        = MSDLL_VERSION;
    // ENTRY НЕ ставим: в тесте тело "DllMain" лежит в .bss, а NX не даст
    // его вызвать. В реальной DLL DllMain всегда в .text — там ENTRY ок.
    h->flags          = MSDLL_FLAG_RELOCS;
    h->image_size     = IMG_SIZE;
    h->entry_rva      = 0;
    h->export_count   = 1;
    h->export_rva     = 0x0040;
    h->import_count   = 1;
    h->import_rva     = 0x0048;
    h->reloc_count    = 1;
    h->reloc_rva      = 0x0054;
    h->section_count  = 0;
    h->section_rva    = 0;

    // Таблица экспортов: name_rva="fake_sym" (0x0074), func_rva=0x0080
    put_u32(0x0040, 0x0074);   // name_rva
    put_u32(0x0044, 0x0080);   // func_rva

    // Таблица импортов: dll_name="mskernel" (0x006A), func="memcpy" (0x0066),
    // iat_rva — неважно для этого теста (мы не патчим IAT).
    put_u32(0x0048, 0x006A);   // dll_name_rva -> "mskernel"
    put_u32(0x004C, 0x0066);   // func_name_rva -> "memcpy"
    put_u32(0x0050, 0x00A0);   // iat_rva (фиктивный, за пределами образа — ок)

    // Релокация ABS32: патчим u32 по RVA 0x0088.
    // Изначально там лежит значение 0x11111111 — после загрузки
    // должно стать 0x11111111 + base.
    put_u32(0x0054, 0x0088);   // rva
    put_u32(0x0058, MSDLL_RELOC_ABS32);

    put_str(0x0060, "fake_func");   // 0x0060
    // 0x006A: "mskernel"
    put_str(0x006A, "mskernel");
    // 0x0074: "fake_sym"
    put_str(0x0074, "fake_sym");
    // 0x0066: "memcpy"
    put_str(0x0066, "memcpy");

    // "Тело функции" по RVA 0x0080 — просто ret (0xC3).
    g_image[0x0080] = 0xC3;

    // Значение, которое должно измениться после релокации.
    put_u32(0x0088, 0x11111111u);
}

// ============================================================
// main
// ============================================================
static int g_fails = 0;
static int g_tests = 0;

static void check(int cond, const char* name) {
    g_tests++;
    if (cond) {
        print("[ OK ] "); println(name);
    } else {
        g_fails++;
        print("[FAIL] "); println(name);
    }
}

// Встроенные экспорты mskernel для теста.
static int builtin_test_fn(void) { return 0xBEEF; }

static const msdll_builtin_export_t TEST_MSKERNEL[] = {
    { "memcpy",    0xDEADBEEF },
    { "test_fn",   (unsigned int)&builtin_test_fn },
    { 0, 0 }
};

static void sys_exit(int code) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(1), "b"(code)
        : "memory"
    );
    for (;;) { }
}

int main(void) {
    println("=== msdll test stand ===");
    println("");

    // 1. init + builtin register
    msdll_init();
    check(msdll_register_builtin("mskernel", TEST_MSKERNEL) == 1,
          "register builtin 'mskernel'");
    check(msdll_symbol_count() == 2,
          "2 symbols registered from builtin");

    // 2. resolve builtin
    unsigned int a1 = msdll_resolve("mskernel", "memcpy");
    check(a1 == 0xDEADBEEF, "resolve mskernel!memcpy");

    unsigned int a2 = msdll_resolve("mskernel", "test_fn");
    check(a2 == (unsigned int)&builtin_test_fn, "resolve mskernel!test_fn");

    // 3. resolve с wildcard
    unsigned int a3 = msdll_resolve(0, "memcpy");
    check(a3 == 0xDEADBEEF, "resolve *!memcpy (dll_name=0)");

    unsigned int a4 = msdll_resolve("*", "test_fn");
    check(a4 == (unsigned int)&builtin_test_fn, "resolve *!test_fn (dll_name=\"*\")");

    // 4. resolve несуществующего
    check(msdll_resolve("mskernel", "nonexistent") == 0,
          "resolve missing symbol returns 0");
    check(msdll_resolve("nodll", "memcpy") == 0,
          "resolve with wrong dll returns 0");

    // 5. validate мусора
    const msdll_header_t* hdr = 0;
    check(msdll_validate(g_image, 0, &hdr) == 0, "validate size=0 fails");
    check(msdll_validate(0, 100, &hdr) == 0, "validate buf=0 fails");

    // 6. build + validate валидного образа
    build_image();
    check(msdll_validate(g_image, IMG_SIZE, &hdr) == 1, "validate well-formed image");
    check(hdr != 0, "validate returns header pointer");
    if (hdr) {
        check(hdr->magic == MSDLL_MAGIC, "header magic correct");
        check(hdr->version == MSDLL_VERSION, "header version correct");
        check(hdr->export_count == 1, "header export_count == 1");
        check(hdr->reloc_count == 1, "header reloc_count == 1");
    }

    // 7. Битый magic
    unsigned char saved = g_image[0];
    g_image[0] = 0xFF;
    check(msdll_validate(g_image, IMG_SIZE, 0) == 0, "validate bad magic fails");
    g_image[0] = saved;

    // 8. Битый version
    unsigned int saved_v = hdr ? hdr->version : 0;
    if (hdr) ((msdll_header_t*)hdr)->version = 99;
    check(msdll_validate(g_image, IMG_SIZE, 0) == 0, "validate bad version fails");
    if (hdr) ((msdll_header_t*)hdr)->version = saved_v;

    // 9. load_from_buffer — релокация должна применить base
    //    "Загружаем" по адресу g_image (identity).
    unsigned int base = (unsigned int)g_image;
    unsigned int before = 0;
    // Читаем u32 по RVA 0x0088 из образа.
    before = (unsigned int)g_image[0x88]
           | ((unsigned int)g_image[0x89] << 8)
           | ((unsigned int)g_image[0x8A] << 16)
           | ((unsigned int)g_image[0x8B] << 24);
    check(before == 0x11111111u, "reloc target starts as 0x11111111");

    unsigned int loaded = msdll_load_from_buffer("fake", g_image, IMG_SIZE, base);
    check(loaded == base, "load_from_buffer returns base");

    // После загрузки символ fake_sym должен резолвиться в base + 0x0080.
    unsigned int sym = msdll_resolve("fake", "fake_sym");
    check(sym == base + 0x0080, "resolve fake!fake_sym == base+0x80");

    // Релокация должна была изменить значение в памяти по RVA 0x0088.
    unsigned int after = (unsigned int)g_image[0x88]
                       | ((unsigned int)g_image[0x89] << 8)
                       | ((unsigned int)g_image[0x8A] << 16)
                       | ((unsigned int)g_image[0x8B] << 24);
    check(after == 0x11111111u + base, "relocation applied: target += base");

    // 10. Загрузка того же образа второй раз — refcount++, база та же.
    unsigned int again = msdll_load_from_buffer("fake", g_image, IMG_SIZE, base + 0x1000);
    check(again == base, "second load returns SAME base (refcount)");

    // 11. Точное количество символов: 2 (mskernel) + 1 (fake) = 3.
    check(msdll_symbol_count() == 3, "3 symbols total");

    // ============================================================
    println("");
    print("Tests:   "); print_int(g_tests); print("\n");
    print("Passed:  "); print_int(g_tests - g_fails); print("\n");
    print("Failed:  "); print_int(g_fails); print("\n");

    // Отладочный вывод: адрес fake_sym в hex (использует print_hex)
    print("fake_sym = ");
    print_hex(msdll_resolve("fake", "fake_sym"));
    print("\n");

    if (g_fails == 0) {
        println("");
        println("ALL TESTS PASSED.");
        return 0;
    }
    println("");
    println("SOME TESTS FAILED.");
    return 1;
}

// ============================================================
// _start — точка входа без CRT
// ============================================================
void _start(void) {
    int rc = main();
    sys_exit(rc);
}
