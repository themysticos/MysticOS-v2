// pe.c - загрузчик PE для MysticOS 2 (PE32, freestanding, без libc).
// См. pe.h. Мультизагрузчик: kernel.c выбирает PE или ELF по магии.
#include "pe.h"

typedef unsigned int  u32;
typedef unsigned char u8;
typedef unsigned short u16;

// --- минимум без libc ---
static void mem_copy(u8* dst, const u8* src, u32 n) {
    for (u32 i = 0; i < n; i++) dst[i] = src[i];
}
static void mem_zero(u8* dst, u32 n) {
    for (u32 i = 0; i < n; i++) dst[i] = 0;
}
static int in_bounds(u32 off, u32 len, u32 size) {
    if (off > size) return 0;
    if (len > size - off) return 0;
    return 1;
}

// Смещение NT-заголовка.
unsigned int pe_nt_offset(const unsigned char* buf, unsigned int size) {
    if (!buf || size < sizeof(pe_dos_header_t)) return 0;
    const pe_dos_header_t* dos = (const pe_dos_header_t*)buf;
    if (dos->e_magic != PE_DOS_MAGIC) return 0;
    u32 nt = dos->e_lfanew;
    if (nt == 0 || nt + 4 + sizeof(pe_file_header_t) > size) return 0;
    return nt;
}

const pe_optional_header32_t* pe_optional(const unsigned char* buf, unsigned int size) {
    u32 nt = pe_nt_offset(buf, size);
    if (!nt) return 0;
    // NT: Signature(4) + FileHeader(20) + OptionalHeader.
    u32 opt_off = nt + 4 + sizeof(pe_file_header_t);
    if (opt_off + sizeof(pe_optional_header32_t) > size) return 0;
    return (const pe_optional_header32_t*)(buf + opt_off);
}

const pe_section_header_t* pe_sections(const unsigned char* buf, unsigned int size) {
    u32 nt = pe_nt_offset(buf, size);
    if (!nt) return 0;
    const pe_file_header_t* fh = (const pe_file_header_t*)(buf + nt + 4);
    u32 opt_off = nt + 4 + sizeof(pe_file_header_t);
    u32 sh_off  = opt_off + fh->SizeOfOptionalHeader;
    if (sh_off + fh->NumberOfSections * sizeof(pe_section_header_t) > size) return 0;
    return (const pe_section_header_t*)(buf + sh_off);
}

// Доступ к DataDirectory[i]. OptionalHeader включает DataDirectory сразу
// после NumberOfRvaAndSizes. Смещение DataDirectory от начала OptionalHeader:
// 96 байт (для PE32).
#define PE_DATADIR_OFF 96u
static const pe_data_dir_t* pe_datadir(const unsigned char* buf, unsigned int size, int idx) {
    const pe_optional_header32_t* opt = pe_optional(buf, size);
    if (!opt) return 0;
    if (idx < 0 || idx >= 16) return 0;
    u32 nt = pe_nt_offset(buf, size);
    u32 off = nt + 4 + sizeof(pe_file_header_t) + PE_DATADIR_OFF + idx * 8;
    if (off + 8 > size) return 0;
    return (const pe_data_dir_t*)(buf + off);
}

int pe_validate(const unsigned char* buf, unsigned int size) {
    if (!buf) return 0;
    u32 nt = pe_nt_offset(buf, size);
    if (!nt) return 0;
    // Signature 'PE\0\0'
    const u8* sig = buf + nt;
    if (!(sig[0] == 'P' && sig[1] == 'E' && sig[2] == 0 && sig[3] == 0)) return 0;
    const pe_file_header_t* fh = (const pe_file_header_t*)(buf + nt + 4);
    if (fh->Machine != PE_MACHINE_I386) return 0;
    if (fh->NumberOfSections == 0 || fh->NumberOfSections > PE_MAX_SECTIONS) return 0;
    const pe_optional_header32_t* opt = pe_optional(buf, size);
    if (!opt) return 0;
    if (opt->Magic != PE_OPT_MAGIC32) return 0;
    if (opt->SizeOfImage == 0 || opt->SizeOfImage > 0x400000u) return 0;   // <= 4 МБ
    if (opt->SectionAlignment > 0x1000) return 0;   // ждём 4 КБ (наша страница — 4 МБ, но так проще)
    return 1;
}

// Перевести RVA в файловое смещение (через таблицу секций).
// 0 если RVA не попадает ни в одну секцию.
static u32 pe_rva_to_off(const unsigned char* image, unsigned int size, u32 rva) {
    const pe_optional_header32_t* opt = pe_optional(image, size);
    const pe_section_header_t* sh = pe_sections(image, size);
    const pe_file_header_t* fh = (const pe_file_header_t*)(image + pe_nt_offset(image, size) + 4);
    if (!opt || !sh || !fh) return 0;
    // Если RVA внутри заголовков (< SizeOfHeaders) — это уже смещение в файле.
    if (rva < opt->SizeOfHeaders) return rva;
    for (int i = 0; i < fh->NumberOfSections; i++) {
        u32 va = sh[i].VirtualAddress;
        u32 vsz = sh[i].VirtualSize;
        if (vsz == 0) vsz = sh[i].SizeOfRawData;
        if (rva >= va && rva < va + vsz) {
            return sh[i].PointerToRawData + (rva - va);
        }
    }
    return 0;
}

// Применить .reloc: delta = new_base - ImageBase. Тип HIGHLOW (3).
// Патчит по phys_base + page_rva + entry_offset. Работает с уже разложенными
// секциями (патчим в памяти).
static void pe_apply_relocs(const unsigned char* image, unsigned int size,
                            unsigned int phys_base, int delta) {
    const pe_data_dir_t* rd = pe_datadir(image, size, PE_DIR_BASERELOC);
    if (!rd || !rd->VirtualAddress || !rd->Size) return;
    u32 off = pe_rva_to_off(image, size, rd->VirtualAddress);   // RVA -> файл
    if (!off) return;
    u32 end = off + rd->Size;
    if (end < off || end > size) return;
    while (off + sizeof(pe_reloc_block_t) <= end) {
        const pe_reloc_block_t* blk = (const pe_reloc_block_t*)(image + off);
        if (blk->SizeOfBlock < sizeof(pe_reloc_block_t)) break;
        u32 nent = (blk->SizeOfBlock - sizeof(pe_reloc_block_t)) / 2;
        const unsigned short* ent = (const unsigned short*)(image + off + sizeof(pe_reloc_block_t));
        for (u32 i = 0; i < nent; i++) {
            u32 type = (ent[i] >> 12) & 0xF;
            u32 ofs  = ent[i] & 0xFFF;
            if (type != IMAGE_REL_BASED_HIGHLOW) continue;
            u32 target = blk->VirtualAddress + ofs;   // RVA
            if (target + 4 > 0x400000u) continue;
            u32* slot = (u32*)(phys_base + target);
            *slot += (u32)delta;
        }
        off += blk->SizeOfBlock;
    }
}

int pe_load_segments(const unsigned char* image, unsigned int size,
                     unsigned int virt_base, unsigned int phys_base) {
    (void)virt_base;   // PE линкуется под ImageBase; virt_base не нужен
    if (!pe_validate(image, size)) return -1;

    const pe_optional_header32_t* opt = pe_optional(image, size);
    const pe_section_header_t* sh = pe_sections(image, size);
    const pe_file_header_t* fh = (const pe_file_header_t*)(image + pe_nt_offset(image, size) + 4);
    if (!opt || !sh) return -1;

    // Зануляем весь образ в памяти (SizeOfImage).
    mem_zero((u8*)phys_base, opt->SizeOfImage);

    // Копируем секции по VirtualAddress (RVA).
    for (int i = 0; i < fh->NumberOfSections; i++) {
        u32 va   = sh[i].VirtualAddress;
        u32 vsz  = sh[i].VirtualSize;
        u32 raw  = sh[i].SizeOfRawData;
        u32 foff = sh[i].PointerToRawData;
        u32 n = (raw < vsz) ? raw : vsz;   // копируем min(raw, vsz)
        if (n == 0) continue;
        if (!in_bounds(foff, n, size)) return -1;
        if (va + n > opt->SizeOfImage) return -1;
        mem_copy((u8*)(phys_base + va), image + foff, n);
        // Хвост (vsz > raw) уже нулевой — мы занулили образ.
    }

    // Релокации: применяем их по ВИРТУАЛЬНОМУ адресу (virt_base), потому что
    // код ИСПОЛНЯЕТСЯ по virt_base (туда мапит PD задачи). Секции лежат в
    // памяти по phys_base, но абсолютные адреса внутри образа должны
    // указывать на виртуальные адреса (virt_base + RVA).
    //
    // Типичный случай: phys_base = code_phys (pmem-блок), virt_base = 0x04000000.
    // ImageBase обычно = 0x04000000 (совпадает с virt_base) → delta=0,
    // релокации не нужны. Если ImageBase другой — сдвигаем на (virt_base-ImageBase).
    int delta = (int)virt_base - (int)opt->ImageBase;
    if (delta != 0) {
        // Патчим по virt_base, но данные физически в phys_base.
        // Т.к. на этапе загрузки virt_base не отображён (PD ещё не активна),
        // пишем через phys_base, но СЧИТАЕМ адреса от virt_base:
        pe_apply_relocs(image, size, phys_base, delta);
    }

    return (int)opt->AddressOfEntryPoint;
}

// ============================================================
// ИМПОРТЫ (.idata) -> IAT
// ============================================================
__attribute__((weak)) extern unsigned int msdll_resolve(const char* dll_name, const char* func_name);
// Авто-загрузка DLL по имени (реализация в kernel.c: vfs + pe_load_dll).
// Возвращает базу DLL или 0. Слабый символ — для хостового теста.
__attribute__((weak)) extern unsigned int pe_load_dll_by_name(const char* name);

static void pe_strip_dll_ext(char* s) {
    int n = 0; while (s[n]) n++;
    if (n >= 4) {
        char a=s[n-4]|0x20, b=s[n-3]|0x20, c=s[n-2]|0x20, d=s[n-1]|0x20;
        if (a==0x2e&&b==0x64&&c==0x6c&&d==0x6c) s[n-4]=0;
    }
}

int pe_load_imports(const unsigned char* image, unsigned int size,
                    unsigned int phys_base) {
    const pe_data_dir_t* id = pe_datadir(image, size, PE_DIR_IMPORT);
    if (!id || !id->VirtualAddress || !id->Size) return 0;
    u32 off = pe_rva_to_off(image, size, id->VirtualAddress);
    if (!off) return -1;
    int resolved = 0;
    for (;;) {
        if (off + sizeof(pe_import_desc_t) > size) break;
        const pe_import_desc_t* d = (const pe_import_desc_t*)(image + off);
        if (d->Name == 0 && d->FirstThunk == 0 && d->OriginalFirstThunk == 0) break;
        u32 noff = pe_rva_to_off(image, size, d->Name);
        char dllbuf[64]; int k = 0;
        if (noff && noff < size) {
            const char* dn = (const char*)(image + noff);
            while (dn[k] && k < 63) { dllbuf[k] = dn[k]; k++; }
        }
        dllbuf[k] = 0;
        pe_strip_dll_ext(dllbuf);
        // Если DLL ещё не загружена — попросить ядро загрузить её с диска.
        if (pe_load_dll_by_name) pe_load_dll_by_name(dllbuf);
        u32 ilt_rva = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
        u32 ilt_off = pe_rva_to_off(image, size, ilt_rva);
        if (!ilt_off) { off += sizeof(pe_import_desc_t); continue; }
        for (u32 i = 0; ; i++) {
            if (ilt_off + i*4 + 4 > size) break;
            u32 e = *(const u32*)(image + ilt_off + i*4);
            if (e == 0) break;
            u32 addr = 0;
            if (!(e & 0x80000000u)) {
                u32 nm = pe_rva_to_off(image, size, e);
                if (nm && nm + 2 < size) {
                    const char* fname = (const char*)(image + nm + 2);
                    if (msdll_resolve) addr = msdll_resolve(dllbuf, fname);
                }
            }
            u32* iat = (u32*)(phys_base + d->FirstThunk + i*4);
            *iat = addr;
            if (addr) resolved++;
        }
        off += sizeof(pe_import_desc_t);
    }
    return resolved;
}

// ============================================================
// PE-DLL: раскладка + регистрация экспортов (.edata)
// ============================================================
void msdll_register_export(const char* dll, const char* func, unsigned int addr);

int pe_load_dll(const unsigned char* image, unsigned int size,
                unsigned int load_base, const char* dll_name) {
    if (!pe_validate(image, size)) return 0;
    const pe_optional_header32_t* opt = pe_optional(image, size);
    const pe_section_header_t* sh = pe_sections(image, size);
    const pe_file_header_t* fh = (const pe_file_header_t*)(image + pe_nt_offset(image, size) + 4);
    if (!opt || !sh) return 0;
    // Разложить секции по load_base.
    mem_zero((u8*)load_base, opt->SizeOfImage);
    for (int i = 0; i < fh->NumberOfSections; i++) {
        u32 va = sh[i].VirtualAddress, vsz = sh[i].VirtualSize;
        u32 raw = sh[i].SizeOfRawData, foff = sh[i].PointerToRawData;
        u32 n = (raw < vsz) ? raw : vsz;
        if (n == 0) continue;
        if (!in_bounds(foff, n, size)) return 0;
        if (va + n > opt->SizeOfImage) return 0;
        mem_copy((u8*)(load_base + va), image + foff, n);
    }
    // Релокации (если ImageBase != load_base).
    int delta = (int)load_base - (int)opt->ImageBase;
    if (delta != 0) pe_apply_relocs(image, size, load_base, delta);
    // Экспорты.
    const pe_data_dir_t* ed = pe_datadir(image, size, PE_DIR_EXPORT);
    if (ed && ed->VirtualAddress && ed->Size) {
        u32 eoff = pe_rva_to_off(image, size, ed->VirtualAddress);
        if (eoff && eoff + sizeof(pe_export_dir_t) <= size) {
            const pe_export_dir_t* ex = (const pe_export_dir_t*)(image + eoff);
            u32 fn_off  = pe_rva_to_off(image, size, ex->AddressOfFunctions);
            u32 nm_off  = pe_rva_to_off(image, size, ex->AddressOfNames);
            u32 ord_off = pe_rva_to_off(image, size, ex->AddressOfNameOrdinals);
            if (fn_off && nm_off && ord_off) {
                for (u32 i = 0; i < ex->NumberOfNames; i++) {
                    if (nm_off + i*4 + 4 > size) break;
                    if (ord_off + i*2 + 2 > size) break;
                    u32 name_rva = *(const u32*)(image + nm_off + i*4);
                    u16 ord = *(const u16*)(image + ord_off + i*2);
                    if (fn_off + ord*4 + 4 > size) continue;
                    u32 func_rva = *(const u32*)(image + fn_off + ord*4);
                    u32 nf = pe_rva_to_off(image, size, name_rva);
                    if (!nf) continue;
                    const char* fname = (const char*)(image + nf);
                    msdll_register_export(dll_name, fname, load_base + func_rva);
                }
            }
        }
    }
    return (int)load_base;
}
