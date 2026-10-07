// test_pe.c - ХОСТОВЫЙ тест PE-загрузчика (pe.c).
//
// Читает .exe, проверяет валидацию и раскладку секций. Выполнять код
// нельзя (он зовёт int 0x80 -> window). Проверяем:
//   * pe_validate
//   * заголовки (ImageBase, entry, секции)
//   * pe_load_segments -> секции в памяти, entry верный
//
// Сборка: gcc -m32 ... test_pe.c pe.c -o test_pe
#include "pe.h"

static int sys_write(int fd, const void* b, unsigned n){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(4),"b"(fd),"c"(b),"d"(n):"memory");return r;}
static void print(const char* s){unsigned n=0;while(s[n])n++;sys_write(1,s,n);}
static void print_int(int v){if(!v){sys_write(1,"0",1);return;}char b[12];int i=0,neg=0;if(v<0){neg=1;v=-v;}while(v){b[i++]='0'+v%10;v/=10;}if(neg)b[i++]='-';char o[12];int j=0;while(i)o[j++]=b[--i];sys_write(1,o,(unsigned)j);}
static void print_hex(unsigned v){const char*h="0123456789ABCDEF";char b[10]={'0','x'};for(int i=0;i<8;i++)b[2+i]=h[(v>>(28-i*4))&15];sys_write(1,b,10);}
static int sys_open(const char* p,int f,int m){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(5),"b"(p),"c"(f),"d"(m):"memory");return r;}
static int sys_read(int fd,void* b,unsigned n){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(3),"b"(fd),"c"(b),"d"(n):"memory");return r;}
static int sys_close(int fd){int r;__asm__ volatile("int $0x80":"=a"(r):"a"(6),"b"(fd):"memory");return r;}

static int g_fail=0,g_tests=0;
static void check(int c,const char* n){g_tests++;if(c){print("[ OK ] ");print(n);print("\n");}else{g_fail++;print("[FAIL] ");print(n);print("\n");}}

static unsigned char g_file[0x40000];
static unsigned char g_mem[0x400000];   // 4 МБ регион

void _start(void){
    print("=== PE loader test ===\n");
    int fd=sys_open("pehello.exe",0,0);
    check(fd>=0,"open pehello.exe");
    if(fd<0){__asm__ volatile("int $0x80"::"a"(1),"b"(1));for(;;);}
    int n=sys_read(fd,g_file,sizeof(g_file));
    sys_close(fd);
    check(n>0,"read .exe");
    print("file size = ");print_int(n);print("\n");

    check(pe_validate(g_file,(unsigned)n)==1,"pe_validate");

    const pe_optional_header32_t* opt=pe_optional(g_file,(unsigned)n);
    check(opt!=0,"pe_optional != 0");
    if(opt){
        print("ImageBase = ");print_hex(opt->ImageBase);print("\n");
        print("SizeOfImage = ");print_hex(opt->SizeOfImage);print("\n");
        print("Entry RVA = ");print_hex(opt->AddressOfEntryPoint);print("\n");
        check(opt->ImageBase==0x04000000,"ImageBase == 0x04000000");
        check(opt->SizeOfImage>0 && opt->SizeOfImage<=0x400000,"SizeOfImage sane");
    }

    // На хосте грузим в реальный буфер g_mem (адрес 0x04000000 не замаплен).
    // phys_base = адрес g_mem -> delta = g_mem - ImageBase (релокации сработают).
    unsigned int mem=(unsigned int)g_mem;
    int entry=pe_load_segments(g_file,(unsigned)n,0x04000000,mem);
    check(entry>=0,"pe_load_segments returns entry");
    print("entry (RVA) = ");print_hex((unsigned)entry);print("\n");

    // В памяти .text начинается с RVA 0x1000. Проверим, что там не нули.
    unsigned char* text=(unsigned char*)(mem+0x1000);
    int nonzero=0; for(int i=0;i<64;i++) if(text[i]) nonzero++;
    check(nonzero>0,".text section loaded (non-zero)");

    // Проверим, что релокации применились: если ImageBase != mem, то
    // абсолютные адреса в .text сдвинуты. Ищем dword, равный mem-базным адресам.
    // Косвенно: загрузка по другому адресу тоже должна пройти.
    int entry2=pe_load_segments(g_file,(unsigned)n,0x04000000,mem+0x100000);
    check(entry2>=0,"pe_load_segments to second region");
    unsigned char* text2=(unsigned char*)(mem+0x100000+0x1000);
    int nz2=0; for(int i=0;i<64;i++) if(text2[i]) nz2++;
    check(nz2>0,".text loaded in second region");

    print("\nTests:  ");print_int(g_tests);
    print("  Failed: ");print_int(g_fail);print("\n");
    if(g_fail==0) print("ALL TESTS PASSED.\n");
    else print("SOME TESTS FAILED.\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(g_fail));
    for(;;){}
}
