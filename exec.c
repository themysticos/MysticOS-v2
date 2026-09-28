// exec.c - загрузчик ELF32 для MysticOS
// Загружает статический ELF32 (executable) из буфера в память по
// адресам PT_LOAD-сегментов и передаёт управление на e_entry.
//
// Требования к программе:
//   * 32-битный ELF, little-endian, статический (без динамики).
//   * Собирается: gcc -m32 -ffreestanding -nostdlib -static -no-pie
//     -Wl,-Ttext=0x400000 -Wl,-e,_start prog.c -o prog.elf
//   * Paging в ядре выключен, поэтому p_vaddr == физический адрес.
//   * Программа возвращает int через ret (соглашение вызова).

#include "api.h"

void print_string(const char* str);
void put_char(char c);
void print_int(int num);
void print_hex(unsigned int num);

// ============================================================
// ELF32 структуры
// ============================================================
#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int   e_version;
    unsigned int   e_entry;
    unsigned int   e_phoff;
    unsigned int   e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    unsigned int   p_type;
    unsigned int   p_offset;
    unsigned int   p_vaddr;
    unsigned int   p_paddr;
    unsigned int   p_filesz;
    unsigned int   p_memsz;
    unsigned int   p_flags;
    unsigned int   p_align;
} __attribute__((packed)) elf32_phdr_t;

#define PT_LOAD 1

// ============================================================
// Загрузка и запуск
// ============================================================

// Копирует память побайтово (memcpy)
static void mem_copy(unsigned char* dst, const unsigned char* src, unsigned int n) {
    for (unsigned int i = 0; i < n; i++) dst[i] = src[i];
}

// Зануляет память (memset)
static void mem_zero(unsigned char* dst, unsigned int n) {
    for (unsigned int i = 0; i < n; i++) dst[i] = 0;
}

// Загрузить PT_LOAD-сегменты ELF по физическому базовому адресу.
// ELF собран с p_vaddr, начиная с virt_base (обычно 0x04000000). Каждый
// сегмент копируется в phys_base + (p_vaddr - virt_base). Возвращает
// виртуальный адрес точки входа (e_entry) или -1.
int exec_load_segments(const unsigned char* image, unsigned int size,
                       unsigned int virt_base, unsigned int phys_base) {
    if (size < sizeof(elf32_ehdr_t)) return -1;
    const elf32_ehdr_t* eh = (const elf32_ehdr_t*)image;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') return -1;
    if (eh->e_ident[4] != 1 || eh->e_machine != 3) return -1;

    for (int i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t* ph =
            (const elf32_phdr_t*)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) return -1;
        if (ph->p_vaddr < virt_base) return -1;
        unsigned int off = ph->p_vaddr - virt_base;
        if (off + ph->p_memsz > 0x400000u) return -1;   // не вылезать за 4 МБ
        unsigned char* dst = (unsigned char*)(phys_base + off);
        mem_copy(dst, image + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            mem_zero(dst + ph->p_filesz, ph->p_memsz - ph->p_filesz);
    }
    return (int)eh->e_entry;
}

int exec_run(const unsigned char* image, unsigned int size, const mystic_api_t* api) {
    if (size < sizeof(elf32_ehdr_t)) {
        print_string("exec: file too small\n");
        return -1;
    }

    const elf32_ehdr_t* eh = (const elf32_ehdr_t*)image;

    // Проверка магии ELF
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        print_string("exec: not an ELF file\n");
        return -1;
    }
    // Класс: 1 = 32-bit
    if (eh->e_ident[4] != 1) {
        print_string("exec: not 32-bit ELF\n");
        return -1;
    }
    // Машина: 3 = x86
    if (eh->e_machine != 3) {
        print_string("exec: not x86\n");
        return -1;
    }

    // Загружаем PT_LOAD-сегменты
    for (int i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t* ph =
            (const elf32_phdr_t*)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_offset + ph->p_filesz > size) {
            print_string("exec: segment out of file bounds\n");
            return -1;
        }

        // Валидация адреса: не даём программе грузиться поверх ядра,
        // нижней RAM или буферов. Разрешаем только область >= 16 МБ.
        #define EXEC_MIN_VADDR 0x04000000u
        if (ph->p_vaddr < EXEC_MIN_VADDR) {
            print_string("exec: segment vaddr too low (would clobber kernel)\n");
            return -1;
        }
        // Проверка на переполнение конца сегмента
        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) {
            print_string("exec: segment address overflow\n");
            return -1;
        }

        unsigned char* dst = (unsigned char*)ph->p_vaddr;
        const unsigned char* src = image + ph->p_offset;

        // Копируем данные
        mem_copy(dst, src, ph->p_filesz);
        // Зануляем .bss (memsz > filesz)
        if (ph->p_memsz > ph->p_filesz) {
            mem_zero(dst + ph->p_filesz, ph->p_memsz - ph->p_filesz);
        }

    }

    // Передаём управление программе через exec_call (сохраняет точку
    // возврата, чтобы можно было принудительно завершить программу).
    // Стек программы — в СВОБОДНОЙ области (выше backbuffer 0x01000000,
    // ниже кода программ 0x04000000). Раньше стояло 0x600000 — а это
    // внутри .bss ядра (.bss доходит до ~0x61d34), программа затирала
    // данные ядра (терминал/окна/задачи) -> "улёт".
    #define EXEC_STACK_BASE 0x00800000u
    #define EXEC_STACK_SIZE 0x00010000u
    unsigned int user_sp = (EXEC_STACK_BASE + EXEC_STACK_SIZE) & ~0xFu;
    int (*entry)(const mystic_api_t*) = (int (*)(const mystic_api_t*))eh->e_entry;
    extern int exec_call(int (*entry)(void*), void* arg, unsigned int stack_top);
    return exec_call((int (*)(void*))entry, (void*)api, user_sp);
}
