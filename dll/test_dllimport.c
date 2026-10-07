// test_dllimport.c - ХОСТОВЫЙ тест: PE-exe импортирует из PE-DLL.
#include "msdll.h"

int pe_validate(const unsigned char*, unsigned int);
int pe_load_dll(const unsigned char*, unsigned int, unsigned int, const char*);
int pe_load_segments(const unsigned char*, unsigned int, unsigned int, unsigned int);
int pe_load_imports(const unsigned char*, unsigned int, unsigned int);

unsigned int pe_load_dll_by_name(const char* name) { (void)name; return 0; }

static int sys_write(int fd,const void* b,unsigned n){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(4),"b"(fd),"c"(b),"d"(n):"memory");return r;}
static void print(const char* s){unsigned n=0;while(s[n])n++;sys_write(1,s,n);}
static void print_int(int v){if(!v){sys_write(1,"0",1);return;}char b[12];int i=0,neg=0;if(v<0){neg=1;v=-v;}while(v){b[i++]='0'+v%10;v/=10;}if(neg)b[i++]='-';char o[12];int j=0;while(i)o[j++]=b[--i];sys_write(1,o,(unsigned)j);}
static int sys_open(const char* p,int f,int m){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(5),"b"(p),"c"(f),"d"(m):"memory");return r;}
static int sys_read(int fd,void* b,unsigned n){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(3),"b"(fd),"c"(b),"d"(n):"memory");return r;}
static int sys_close(int fd){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(6),"b"(fd):"memory");return r;}
static int sys_mprotect(void* a,unsigned l,int p){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(125),"b"(a),"c"(l),"d"(p):"memory");return r;}

static int g_fail=0,g_tests=0;
static void check(int c,const char* n){g_tests++;if(c){print("[ OK ] ");print(n);print("\n");}else{g_fail++;print("[FAIL] ");print(n);print("\n");}}

static unsigned char g_dll[0x40000];
static unsigned char g_exe[0x40000];
static unsigned char g_region[0x200000] __attribute__((aligned(4096)));
static unsigned char g_dllregion[0x100000] __attribute__((aligned(4096)));

void _start(void) {
    print("=== PE import test (exe -> dll) ===\n");
    sys_mprotect(g_region, sizeof(g_region), 7);
    sys_mprotect(g_dllregion, sizeof(g_dllregion), 7);

    int fd = sys_open("testdll.dll", 0, 0);
    check(fd >= 0, "open testdll.dll");
    int dn = sys_read(fd, g_dll, sizeof(g_dll)); sys_close(fd);
    check(dn > 0, "read testdll.dll");

    msdll_init();
    unsigned int dbase = (unsigned int)g_dllregion;
    int dl = pe_load_dll(g_dll, (unsigned int)dn, dbase, "testdll");
    check(dl > 0, "pe_load_dll loaded");

    unsigned int a_add = msdll_resolve("testdll", "my_add");
    unsigned int a_mul = msdll_resolve("testdll", "my_mul");
    check(a_add != 0, "resolve testdll!my_add");
    check(a_mul != 0, "resolve testdll!my_mul");

    fd = sys_open("testexe.exe", 0, 0);
    check(fd >= 0, "open testexe.exe");
    int en = sys_read(fd, g_exe, sizeof(g_exe)); sys_close(fd);
    check(en > 0, "read testexe.exe");

    unsigned int ebase = (unsigned int)g_region;
    int entry = pe_load_segments(g_exe, (unsigned int)en, ebase, ebase);
    check(entry >= 0, "pe_load_segments(exe)");

    int nimp = pe_load_imports(g_exe, (unsigned int)en, ebase);
    check(nimp == 2, "pe_load_imports resolved 2");

    unsigned int* iat_add = (unsigned int*)(ebase + 0x4034);
    unsigned int* iat_mul = (unsigned int*)(ebase + 0x4038);
    check(*iat_add == a_add, "IAT[my_add] == resolved addr");
    check(*iat_mul == a_mul, "IAT[my_mul] == resolved addr");

    int (*f_add)(int,int) = (int (*)(int,int))*iat_add;
    int (*f_mul)(int,int) = (int (*)(int,int))*iat_mul;
    check(f_add(2,3) == 5, "call my_add(2,3) == 5");
    check(f_mul(4,5) == 20, "call my_mul(4,5) == 20");

    print("\nTests:  "); print_int(g_tests);
    print("  Failed: "); print_int(g_fail); print("\n");
    if (g_fail == 0) print("ALL TESTS PASSED.\n");
    else print("SOME TESTS FAILED.\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(g_fail));
    for(;;){}
}
