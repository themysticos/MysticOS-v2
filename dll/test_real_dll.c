// test_real_dll.c - ХОСТОВЫЙ тест: загрузить НАСТОЯЩИЙ .msdll и вызвать.
//
// Читает example_msgui.msdll, грузит через msdll_load_image() в свой регион,
// резолвит msgui_add и вызывает. Проверяет всю цепочку:
//   gcc -> msdll.ld -> msdll_pack.py -> msdll_load_image -> вызов.
//
// Собирается: gcc -m32 ... test_real_dll.c msdll.c -o test_real_dll
// Требует gcc-multilib.
#include "msdll.h"

// --- минимальный вывод + чтение файла через Linux syscalls (int 0x80) ---
static int sys_write(int fd, const void* buf, unsigned int n) {
    int r; __asm__ volatile("int $0x80":"=a"(r):"a"(4),"b"(fd),"c"(buf),"d"(n):"memory");
    return r;
}
static void print(const char* s){ unsigned n=0; while(s[n])n++; sys_write(1,s,n); }
static void print_int(int v){
    if(!v){sys_write(1,"0",1);return;} char b[12]; int i=0,neg=0;
    if(v<0){neg=1;v=-v;} while(v){b[i++]='0'+v%10;v/=10;}
    if(neg)b[i++]='-';
    char o[12]; int j=0; while(i)o[j++]=b[--i];
    sys_write(1,o,(unsigned)j);
}
static void print_hex(unsigned v){const char*h="0123456789ABCDEF";char b[10]={'0','x'};for(int i=0;i<8;i++)b[2+i]=h[(v>>(28-i*4))&15];sys_write(1,b,10);}

// open/read/close
static int sys_open(const char* p, int fl, int md){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(5),"b"(p),"c"(fl),"d"(md):"memory");return r;}
static int sys_read(int fd, void* b, unsigned n){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(3),"b"(fd),"c"(b),"d"(n):"memory");return r;}
static int sys_close(int fd){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(6),"b"(fd):"memory");return r;}
// mprotect(addr, len, prot) — чтобы регион был исполняемым (хостовый NX).
static int sys_mprotect(void* a, unsigned len, int prot){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(125),"b"(a),"c"(len),"d"(prot):"memory");return r;}

static int g_fail = 0, g_tests = 0;
static void check(int c, const char* n){
    g_tests++;
    if(c){ print("[ OK ] "); print(n); print("\n"); }
    else { g_fail++; print("[FAIL] "); print(n); print("\n"); }
}

// Регион DLL — статический буфер (на хосте 0x64000000 недоступен).
// В .bss (writable), а на исполняемость переводим mprotect'ом — иначе
// хостовый NX не даст вызвать загруженный код. В MysticOS это не нужно.
static unsigned char g_region[0x100000] __attribute__((aligned(4096)));
static unsigned char g_file[0x100000];

void _start(void) {
    print("=== real .msdll load test ===\n");

    int fd = sys_open("example_msgui.msdll", 0, 0);
    check(fd >= 0, "open example_msgui.msdll");
    if (fd < 0) { __asm__ volatile("int $0x80"::"a"(1),"b"(1)); for(;;); }
    int n = sys_read(fd, g_file, sizeof(g_file));
    sys_close(fd);
    check(n > 0, "read file");
    print("file size = "); print_int(n); print("\n");

    msdll_init();
    msdll_region_set((unsigned int)g_region, sizeof(g_region));
    print("g_region = "); print_hex((unsigned int)g_region); print("\n");
    // Хостовый NX: делаем регион RWX, иначе вызов кода из него — SIGSEGV.
    // В MysticOS страницы DLL исполняемы, этот вызов не нужен.
    int mpr = sys_mprotect(g_region, sizeof(g_region), 7 /*RWX*/);
    check(mpr == 0, "mprotect region RWX");

    const msdll_header_t* hdr = 0;
    int ok = msdll_validate(g_file, (unsigned int)n, &hdr);
    check(ok, "msdll_validate(real file)");
    check(hdr && hdr->export_count == 2, "header has 2 exports");
    check(hdr && hdr->reloc_count > 0, "header has relocations");

    // В MysticOS DllMain (_start) вызывается и работает. На хосте код
    // грузится в .bss — снимаем флаг ENTRY, чтобы load_image не вызвал
    // DllMain (на хосте это не критично для проверки resolve/call).
    if (hdr) {
        unsigned int* flags = (unsigned int*)(g_file + 8);
        *flags &= ~MSDLL_FLAG_ENTRY;
    }

    unsigned int base = msdll_load_image("msgui", g_file, (unsigned int)n);
    check(base != 0, "load_image returns base");
    print("base = "); print_hex(base); print("\n");

    unsigned int a = msdll_resolve("msgui", "add");
    unsigned int v = msdll_resolve("msgui", "version");
    check(a != 0, "resolve msgui!add");
    check(v != 0, "resolve msgui!version");

    if (a && v) {
        int (*f_add)(int,int) = (int (*)(int,int))a;
        int (*f_ver)(void) = (int (*)(void))v;
        check(f_ver() == 1, "call msgui_version() == 1");
        // msgui_add(a,b): g_counter(0x1000) += a; return a+b+g_counter.
        // Первый вызов: g_counter=0x1000+2=0x1002; return 2+3+0x1002 = 0x1007.
        int r = f_add(2, 3);
        print("msgui_add(2,3) = "); print_int(r); print("\n");
        check(r == 0x1007, "call msgui_add(2,3) == 0x1007");
        // Второй вызов: g_counter=0x1002+2=0x1004; return 2+3+0x1004 = 0x1009.
        int r2 = f_add(2, 3);
        check(r2 == 0x1009, "g_counter persists across calls");
    }

    print("\nTests:  "); print_int(g_tests);
    print("  Failed: "); print_int(g_fail); print("\n");
    if (g_fail == 0) print("ALL TESTS PASSED.\n");
    else print("SOME TESTS FAILED.\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(g_fail));
    for(;;){}
}
