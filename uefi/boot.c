// boot.c - UEFI-загрузчик MysticOS 2 (BOOTX64.EFI) в стиле gnu-efi.
// Использует -lefi: InitializeLib, uefi_call_wrapper, Print, стандартные GUID.
// БЕЗ перехода в 32-бит: работает в long mode, прыгает в ядро в long mode.
//
// ВАЖНО: efi_main БЕЗ EFIAPI (как в рабочем hello.c) — иначе StartImage
// даёт "Invalid Parameter".
//
// Что делает: GOP -> boot-параметры 0x9000; читает \kernel.elf; грузит
// ELF64 PT_LOAD; ExitBootServices (2 вызова GetMemoryMap); прыжок в ядро.
//
// Пока ядро 32-битное, оно упадёт после прыжка — это ожидаемо до порта.

#include <efi.h>
#include <efilib.h>

// Точка входа в ядро (boot_helpers.asm).
extern void uefi_jump64(UINT64 entry);

// ---- Вывод в COM1 (serial) ----
static inline void outb(unsigned short port, unsigned char v){
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline unsigned char inb(unsigned short port){
    unsigned char r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}
static void ser_init(void){
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x80); outb(0x3F8+0,0x01);
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x03); outb(0x3F8+2,0xC7);
    outb(0x3F8+4,0x0B);
}
static void ser_putc(char c){
    for(int i=0;i<100000;i++){ if(inb(0x3F8+5)&0x20) break; }
    outb(0x3F8,(unsigned char)c);
    outb(0xE9,(unsigned char)c);
}
static void ser_puts(const char* s){ while(*s){ if(*s=='\n') ser_putc('\r'); ser_putc(*s++); } }
static void ser_hex(UINT64 v){ char b[19]; b[0]='0'; b[1]='x'; for(int i=0;i<16;i++){ int n=(v>>((15-i)*4))&0xF; b[2+i]=n<10?'0'+n:'a'+n-10; } b[18]=0; ser_puts(b); }

// ---- Память ----
static void memcpy8(void* d, const void* s, unsigned int n){ unsigned char*D=d; const unsigned char*S=s; for(unsigned i=0;i<n;i++) D[i]=S[i]; }
static void memset8(void* d, unsigned char v, unsigned int n){ unsigned char*D=d; for(unsigned i=0;i<n;i++) D[i]=v; }

// ---- ELF64 ----
typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type, e_machine;
    UINT32 e_version;
    UINT64 e_entry, e_phoff, e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    UINT32 p_type, p_flags;
    UINT64 p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} __attribute__((packed)) elf64_phdr_t;

#define PT_LOAD 1
#define EM_X86_64 62
#define PAGE_SIZE 4096ULL

// ---- boot params -> 0x9000 ----
static void fill_boot_params(EFI_GRAPHICS_OUTPUT_PROTOCOL* gop){
    volatile UINT8* p = (volatile UINT8*)0x9000;
    memset8((void*)0x9000, 0, 64);
    UINT32 pitch = gop->Mode->Info->PixelsPerScanLine * 4u;
    *(volatile UINT32*)(p+0x00) = (UINT32)gop->Mode->FrameBufferBase;
    *(volatile UINT16*)(p+0x04) = (UINT16)(pitch & 0xFFFF);
    *(volatile UINT16*)(p+0x06) = (UINT16)gop->Mode->Info->HorizontalResolution;
    *(volatile UINT16*)(p+0x08) = (UINT16)gop->Mode->Info->VerticalResolution;
    p[0x0A] = 32;   // bpp
    p[0x0B] = 0;    // 0=BGRA
    p[0x0C] = 16; p[0x0D] = 8;
    p[0x0E] = 8;  p[0x0F] = 8;
    p[0x10] = 0;  p[0x11] = 8;
}

// ---- read_file: читает файл целиком, чисто освобождает при ошибке ----
static EFI_STATUS read_file(EFI_HANDLE img, CHAR16* path, void** out, UINTN* out_size){
    *out = 0; *out_size = 0;
    EFI_LOADED_IMAGE* li = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->HandleProtocol, 3, img, &LoadedImageProtocol, (void**)&li);
    if(EFI_ERROR(st)) return st;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = 0;
    st = uefi_call_wrapper(BS->HandleProtocol, 3, li->DeviceHandle, &FileSystemProtocol, (void**)&fs);
    if(EFI_ERROR(st)) return st;

    EFI_FILE_HANDLE dir = 0;
    st = uefi_call_wrapper(fs->OpenVolume, 2, fs, &dir);
    if(EFI_ERROR(st)) return st;

    EFI_FILE_HANDLE f = 0;
    st = uefi_call_wrapper(dir->Open, 5, dir, &f, path, EFI_FILE_MODE_READ, 0);
    if(EFI_ERROR(st)) return st;

    UINTN isz = sizeof(EFI_FILE_INFO) + 512;
    void* info = 0;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, isz, &info);
    if(EFI_ERROR(st)){ uefi_call_wrapper(f->Close, 1, f); return st; }
    st = uefi_call_wrapper(f->GetInfo, 4, f, &GenericFileInfo, &isz, info);
    if(EFI_ERROR(st)){ uefi_call_wrapper(BS->FreePool, 1, info); uefi_call_wrapper(f->Close, 1, f); return st; }
    UINTN sz = ((EFI_FILE_INFO*)info)->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, info);

    void* buf = 0;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, sz, &buf);
    if(EFI_ERROR(st)){ uefi_call_wrapper(f->Close, 1, f); return st; }

    UINTN got = sz;
    st = uefi_call_wrapper(f->Read, 3, f, &got, buf);
    uefi_call_wrapper(f->Close, 1, f);
    if(EFI_ERROR(st)){ uefi_call_wrapper(BS->FreePool, 1, buf); return st; }
    if(got != sz){ uefi_call_wrapper(BS->FreePool, 1, buf); return EFI_LOAD_ERROR; }

    *out = buf; *out_size = sz;
    return EFI_SUCCESS;
}

// ---- load_elf64: PT_LOAD -> AllocatePages + копирование + .bss ----
static UINT64 load_elf64(UINT8* img, UINTN size){
    if(size < sizeof(elf64_ehdr_t)) return 0;
    elf64_ehdr_t* eh = (elf64_ehdr_t*)img;
    if(eh->e_ident[0]!=0x7F||eh->e_ident[1]!='E'||eh->e_ident[2]!='L'||eh->e_ident[3]!='F') return 0;
    if(eh->e_ident[4]!=2) return 0;
    if(eh->e_machine!=EM_X86_64) return 0;
    if(eh->e_phentsize != sizeof(elf64_phdr_t)) return 0;

    for(UINT16 i=0;i<eh->e_phnum;i++){
        elf64_phdr_t* ph = (elf64_phdr_t*)(img + eh->e_phoff + (UINT64)i*eh->e_phentsize);
        if(ph->p_type != PT_LOAD) continue;
        if(ph->p_memsz == 0) continue;

        UINT64 seg_start = ph->p_vaddr & ~(PAGE_SIZE-1);
        UINT64 seg_end   = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE-1) & ~(PAGE_SIZE-1);
        UINTN  pages     = (UINTN)((seg_end - seg_start) / PAGE_SIZE);

        EFI_PHYSICAL_ADDRESS addr = seg_start;
        EFI_STATUS st = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, pages, &addr);
        if(EFI_ERROR(st)){
            ser_puts("[uefi] AllocatePages fail at "); ser_hex(seg_start); ser_puts("\n");
            return 0;
        }
        memcpy8((void*)(UINTN)ph->p_vaddr, img + ph->p_offset, (unsigned int)ph->p_filesz);
        if(ph->p_memsz > ph->p_filesz)
            memset8((void*)(UINTN)(ph->p_vaddr + ph->p_filesz), 0, (unsigned int)(ph->p_memsz - ph->p_filesz));
    }
    return eh->e_entry;
}

// debugcon: короткие метки этапов (видно в -debugcon file).
#define dbg(s) Print(L##s)

// ---- Точка входа (БЕЗ EFIAPI!) ----
EFI_STATUS efi_main(EFI_HANDLE img, EFI_SYSTEM_TABLE* st){
    InitializeLib(img, st);
    dbg("\n[1] efi_main, InitializeLib ok\n");

    // --- GOP ---
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = 0;
    EFI_STATUS s = uefi_call_wrapper(BS->LocateProtocol, 3, &GraphicsOutputProtocol, 0, (void**)&gop);
    if(EFI_ERROR(s) || !gop){ dbg("[E] no GOP\n"); for(;;){} }
    dbg("[2] GOP ok\n");
    // TEST: fill_boot_params временно отключён (проверка 0x9000).
    // fill_boot_params(gop);
    dbg("[3] skip fill (test)\n");

    // --- kernel.elf ---
    void* kbuf = 0; UINTN ksz = 0;
    s = read_file(img, L"\\kernel.elf", &kbuf, &ksz);
    if(EFI_ERROR(s)){ dbg("[E] kernel.elf not found\n"); for(;;){} }
    dbg("[4] kernel.elf read ok\n");

    UINT64 entry = load_elf64((UINT8*)kbuf, ksz);
    if(!entry){ dbg("[E] bad ELF64\n"); for(;;){} }
    dbg("[5] ELF64 loaded, entry=");
    ser_hex(entry);
    dbg("\n");

    // --- ExitBootServices (2 вызова GetMemoryMap) ---
    UINTN msz = 0, mkey = 0, dsz = 0; UINT32 dver = 0;
    s = uefi_call_wrapper(BS->GetMemoryMap, 5, &msz, 0, &mkey, &dsz, &dver);
    if(s != EFI_BUFFER_TOO_SMALL && EFI_ERROR(s)){ dbg("[E] GMM1\n"); for(;;){} }
    msz += 4 * dsz + 4096;
    void* mmap = 0;
    s = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, msz, &mmap);
    if(EFI_ERROR(s)){ dbg("[E] alloc mmap\n"); for(;;){} }
    s = uefi_call_wrapper(BS->GetMemoryMap, 5, &msz, (EFI_MEMORY_DESCRIPTOR*)mmap, &mkey, &dsz, &dver);
    if(EFI_ERROR(s)){ dbg("[E] GMM2\n"); for(;;){} }
    s = uefi_call_wrapper(BS->ExitBootServices, 2, img, mkey);
    if(EFI_ERROR(s)){ dbg("[E] EBS\n"); for(;;){} }

    dbg("[6] EBS ok, jumping to kernel\n");
    uefi_jump64(entry);
    for(;;){}
    return EFI_SUCCESS;
}
