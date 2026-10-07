// pe.h - загрузчик PE (Portable Executable) для MysticOS 2.
//
// Грузит PE32 (не PE32+) .exe/.dll, скомпилированные под MysticOS
// (i686-w64-mingw32-gcc, но БЕЗ Windows CRT — с нашим API/syscall'ами).
//
// НЕ путать с ELF: ELF грузит exec.c (остаётся для совместимости).
// Мультизагрузчик в kernel.c выбирает по магии: 'MZ' -> PE, 0x7F'ELF' -> ELF.
//
// Что поддерживается:
//   * PE32 (IMAGE_NT_HEADERS, OptionalHeader Magic=0x10B)
//   * секции (раскладка по VirtualAddress)
//   * релокации .reloc (IMAGE_REL_BASED_HIGHLOW = 3)
//   * импорты (.idata) -> IAT (v2)
//   * экспорты (.edata) — для DLL (v2)
//
// WinAPI-совместимость — ОТДЕЛЬНЫЙ слой (потом). Сейчас программа использует
// наш API (int 0x80 через include/ms/*.h).
#ifndef PE_H
#define PE_H

// ============================================================
// PE-структуры (упакованные, little-endian)
// ============================================================
#define PE_DOS_MAGIC   0x5A4Du   // 'MZ'
#define PE_NT_SIGNATURE 0x00004550u  // 'PE\0\0'
#define PE_OPT_MAGIC32 0x10Bu    // PE32
#define PE_MACHINE_I386 0x014Cu

#define PE_MAX_SECTIONS 16

// DOS-заголовок: нам нужно только e_lfanew (смещение NT-заголовка).
typedef struct {
    unsigned short e_magic;    // 'MZ'
    unsigned short e_cblp;
    unsigned short e_cp;
    unsigned short e_crlc;
    unsigned short e_cparhdr;
    unsigned short e_minalloc;
    unsigned short e_maxalloc;
    unsigned short e_ss;
    unsigned short e_sp;
    unsigned short e_csum;
    unsigned short e_ip;
    unsigned short e_cs;
    unsigned short e_lfarlc;
    unsigned short e_ovno;
    unsigned short e_res[4];
    unsigned short e_oemid;
    unsigned short e_oeminfo;
    unsigned short e_res2[10];
    unsigned int   e_lfanew;   // смещение IMAGE_NT_HEADERS
} __attribute__((packed)) pe_dos_header_t;

typedef struct {
    unsigned short Machine;
    unsigned short NumberOfSections;
    unsigned int   TimeDateStamp;
    unsigned int   PointerToSymbolTable;
    unsigned int   NumberOfSymbols;
    unsigned short SizeOfOptionalHeader;
    unsigned short Characteristics;
} __attribute__((packed)) pe_file_header_t;

typedef struct {
    unsigned short Magic;
    unsigned char  MajorLinkerVersion;
    unsigned char  MinorLinkerVersion;
    unsigned int   SizeOfCode;
    unsigned int   SizeOfInitializedData;
    unsigned int   SizeOfUninitializedData;
    unsigned int   AddressOfEntryPoint;   // RVA точки входа
    unsigned int   BaseOfCode;
    unsigned int   BaseOfData;
    unsigned int   ImageBase;             // предпочтительная база
    unsigned int   SectionAlignment;
    unsigned int   FileAlignment;
    unsigned short MajorOperatingSystemVersion;
    unsigned short MinorOperatingSystemVersion;
    unsigned short MajorImageVersion;
    unsigned short MinorImageVersion;
    unsigned short MajorSubsystemVersion;
    unsigned short MinorSubsystemVersion;
    unsigned int   Win32VersionValue;
    unsigned int   SizeOfImage;           // полный размер образа в памяти
    unsigned int   SizeOfHeaders;         // заголовки (до первой секции)
    unsigned int   CheckSum;
    unsigned short Subsystem;
    unsigned short DllCharacteristics;
    unsigned int   SizeOfStackReserve;
    unsigned int   SizeOfStackCommit;
    unsigned int   SizeOfHeapReserve;
    unsigned int   SizeOfHeapCommit;
    unsigned int   LoaderFlags;
    unsigned int   NumberOfRvaAndSizes;
    // DataDirectory[16] идёт дальше — читаем выборочно по смещению.
} __attribute__((packed)) pe_optional_header32_t;

typedef struct {
    unsigned char  Name[8];
    unsigned int   VirtualSize;
    unsigned int   VirtualAddress;   // RVA
    unsigned int   SizeOfRawData;
    unsigned int   PointerToRawData; // смещение в файле
    unsigned int   PointerToRelocations;
    unsigned int   PointerToLinenumbers;
    unsigned short NumberOfRelocations;
    unsigned short NumberOfLinenumbers;
    unsigned int   Characteristics;
} __attribute__((packed)) pe_section_header_t;

// Data directory (только нужные индексы).
#define PE_DIR_EXPORT   0
#define PE_DIR_IMPORT   1
#define PE_DIR_BASERELOC 5

typedef struct {
    unsigned int VirtualAddress;   // RVA
    unsigned int Size;
} __attribute__((packed)) pe_data_dir_t;

// Base relocation block.
typedef struct {
    unsigned int VirtualAddress;   // RVA страницы
    unsigned int SizeOfBlock;      // размер блока (вкл. этот заголовок)
    // далее: unsigned short entries[] (тип<<12 | offset)
} __attribute__((packed)) pe_reloc_block_t;

#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_HIGHLOW  3

// Импорт: дескриптор DLL.
typedef struct {
    unsigned int OriginalFirstThunk;  // RVA ILT (Import Lookup Table)
    unsigned int TimeDateStamp;
    unsigned int ForwarderChain;
    unsigned int Name;                // RVA имени DLL
    unsigned int FirstThunk;          // RVA IAT (Import Address Table)
} __attribute__((packed)) pe_import_desc_t;

// ============================================================
// API загрузчика
// ============================================================

// Проверить, что буфер — валидный PE32. 1/0.
int pe_validate(const unsigned char* buf, unsigned int size);

// Разложить секции PE по phys_base (identity: virt == phys).
// Возвращает RVA точки входа (AddressOfEntryPoint) или -1 при ошибке.
// Секции копируются в phys_base + VirtualAddress.
// Если нужен сдвиг (ImageBase != phys_base), применяет релокации .reloc.
int pe_load_segments(const unsigned char* image, unsigned int size,
                     unsigned int virt_base, unsigned int phys_base);

// Смещение NT-заголовка в буфере (0 если не PE).
unsigned int pe_nt_offset(const unsigned char* buf, unsigned int size);

// Указатели на заголовки внутри буфера (0 при ошибке).
const pe_optional_header32_t* pe_optional(const unsigned char* buf, unsigned int size);
const pe_section_header_t*    pe_sections(const unsigned char* buf, unsigned int size);

// Разрешить импорты (.idata): для каждого импорта найти адрес через
// msdll_resolve и записать в слот IAT. Возвращает число или -1.
int pe_load_imports(const unsigned char* image, unsigned int size,
                    unsigned int phys_base);

// Export directory (IMAGE_EXPORT_DIRECTORY).
typedef struct {
    unsigned int Characteristics;
    unsigned int TimeDateStamp;
    unsigned short MajorVersion;
    unsigned short MinorVersion;
    unsigned int Name;              // RVA имени DLL
    unsigned int Base;
    unsigned int NumberOfFunctions;
    unsigned int NumberOfNames;
    unsigned int AddressOfFunctions;    // RVA массива RVA функций
    unsigned int AddressOfNames;        // RVA массива RVA имён
    unsigned int AddressOfNameOrdinals; // RVA массива u16 ординалов
} __attribute__((packed)) pe_export_dir_t;

// Загрузить PE-DLL по load_base (identity), зарегистрировать экспорты
// в msdll (имя без ".dll"). Возвращает load_base или 0.
int pe_load_dll(const unsigned char* image, unsigned int size,
                unsigned int load_base, const char* dll_name);

#endif // PE_H
