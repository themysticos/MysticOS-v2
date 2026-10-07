#include <efi.h>
#include <efilib.h>
static inline void outb(unsigned short p, unsigned char v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline unsigned char inb(unsigned short p){unsigned char r;__asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p));return r;}
static void sp(const char* s){while(*s){if(*s=='\n'){while(!(inb(0x3FD)&0x20)){}outb(0x3F8,'\r');}while(!(inb(0x3FD)&0x20)){}outb(0x3F8,*s++);}}
static void dbg(char c){ outb(0xE9, (unsigned char)c); }
EFI_STATUS efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE* st){
    dbg('H'); dbg('i'); dbg('\n');
    outb(0x3F9,0);outb(0x3FB,0x80);outb(0x3F8,1);outb(0x3F9,0);outb(0x3FB,3);outb(0x3FA,0xC7);outb(0x3FC,0x0B);
    sp("[hello] efi_main reached\n");
    InitializeLib(img,st);
    Print(L"[hello] Print works\r\n");
    sp("[hello] Print done\n");
    for(;;){}
    return EFI_SUCCESS;
}
