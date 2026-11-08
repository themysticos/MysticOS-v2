// ============================================================================
// mmdcm.c - MMDCM: MysticOS MS-DOS Compatibility Module
// ============================================================================
//
// V86 (Virtual 8086): CPU сам исполняет 16-битный real-mode код, ядро
// перехватывает int 10h/16h/1Ah/21h и эмулирует BIOS/DOS.
// VME даёт #GP на 'CD n'; адрес возврата НЕ кладётся на стек -> eip+=2.
// ============================================================================
#include "mmdcm.h"
#include "serial.h"
#include "pmem.h"
#include "paging.h"
#include "idt.h"   // registers_t
#include "gdt.h"   // gdt_set_esp0

extern int vfs_read_file(const char* path, void* buf, unsigned int max);

extern void mmdcm_enter_v86(unsigned int pd_phys, unsigned int cs, unsigned int ip,
                            unsigned int ss, unsigned int sp, unsigned int ds_es);
extern void mmdcm_leave_v86(void);

static unsigned char g_v86_exc_stack[16384] __attribute__((aligned(16)));
static unsigned int  g_v86_exc_stack_top = 0;

int g_v86_active = 0;
static volatile int g_v86_exited = 0;

#define TRAP_BASE  0x3000

// Физические адреса DOS-памяти и её PD (для CR3). 0 = identity fallback.
static unsigned int g_dos_phys = 0;
static unsigned int g_v86_pd_phys = 0;

// Состояние последнего запуска.
static unsigned int g_last_exit = 0;
static const char*  g_last_error = 0;
static void set_err(const char* e) { g_last_error = e; }

// Сегмент загрузки DOS-программ: 0x1000 -> линейный 0x10000.
#define DOS_LOAD_SEG   0x1000u
#define DOS_LOAD_OFF   0x0100u

static inline unsigned int dos_lin(unsigned int seg, unsigned int off) {
    return ((seg & 0xFFFFu) << 4) + (off & 0xFFFFu);
}

static void sputc(char c) { char s[2] = { c, 0 }; serial_print(s); }
static void sputs(const char* s) { serial_print(s); }
static void sputh(unsigned int v) { serial_print_hex(v); }
static void sputi(int v) { serial_print_int(v); }

int mmdcm_has_vme(void) {
    unsigned int eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    return (ecx & 1u) ? 1 : 0;
}

// ============================================================
// Эмуляция DOS/BIOS. 1 = вернуться в V86, 0 = выход.
// ============================================================
static int emulate_int(int n, registers_t* r) {
    unsigned char ah = (unsigned char)((r->eax >> 8) & 0xFF);

    if (n == 0x20) {                       // DOS: завершить программу
        sputs("[mmdcm] int 20h -> exit\n");
        g_last_exit = 0;
        return 0;
    }
    if (n == 0x21) {
        switch (ah) {
        case 0x00:                         // старое завершение
            sputs("[mmdcm] int 21h:00 -> exit\n");
            g_last_exit = 0;
            return 0;
        case 0x4C:                         // завершение с кодом AL
            g_last_exit = (unsigned int)(r->eax & 0xFF);
            sputs("[mmdcm] int 21h:4C exit="); sputi((int)g_last_exit); sputs("\n");
            return 0;
        case 0x02:                         // вывод символа DL
            sputc((char)(r->edx & 0xFF));
            return 1;
        case 0x09: {                       // вывод строки DS:DX до 0x24
            unsigned int seg = r->ds & 0xFFFF;
            unsigned int off = r->edx & 0xFFFF;
            unsigned int lin = dos_lin(seg, off);
            if (lin < 0x100000u) {
                const char* p = (const char*)lin;
                int guard = 0;
                while (*p != '\x24' && guard++ < 4096) sputc(*p++);
            }
            r->eax = (r->eax & 0xFFFFFF00u) | '\x24';
            return 1;
        }
        case 0x01: case 0x07: case 0x08:   // ввод (нет клавы -> 0)
            r->eax = (r->eax & 0xFFFFFF00u);
            return 1;
        case 0x0E:                         // выбрать диск -> AL=1
            r->eax = (r->eax & 0xFFFFFF00u) | 1u;
            return 1;
        case 0x19:                         // текущий диск -> A:
            r->eax = (r->eax & 0xFFFFFF00u);
            return 1;
        case 0x1A:                         // установить DTA (игнор)
            return 1;
        case 0x25:                         // установить вектор (игнор)
            return 1;
        case 0x2A:                         // дата: 1980-01-01
            r->ecx = 1980;
            r->edx = (1u << 8) | 1u;
            r->eax = (r->eax & 0xFFFFFF00u) | 2u;
            return 1;
        case 0x2C:                         // время 00:00:00
            r->ecx = 0; r->edx = 0;
            return 1;
        case 0x2F:                         // DTA -> ES:BX (ES в кадре нет)
            r->ebx = 0x80;
            return 1;
        case 0x30:                         // версия DOS 5.0
            r->eax = (r->eax & 0xFFFF0000u) | 0x0005u;
            r->ebx = 0; r->ecx = 0;
            return 1;
        case 0x33:                         // Ctrl-Break -> выкл
            r->eax = (r->eax & 0xFFFFFF00u);
            r->edx = (r->edx & 0xFFFF0000u);
            return 1;
        case 0x35:                         // вектор -> ES:BX (ES в кадре нет)
            r->ebx = TRAP_BASE + (r->eax & 0xFF);
            return 1;
        case 0x3D:                         // открыть файл (нет)
            r->eflags |= 1u;
            r->eax = (r->eax & 0xFFFF0000u) | 2u;
            return 1;
        case 0x3E:                         // закрыть файл
            r->eflags &= ~1u;
            return 1;
        case 0x3F:                         // читать файл -> 0
            r->eflags &= ~1u;
            r->eax = (r->eax & 0xFFFF0000u);
            return 1;
        case 0x40:                         // писать в handle (игнор)
            return 1;
        case 0x44:                         // IOCTL -> не устройство
            r->eflags |= 1u;
            r->eax = (r->eax & 0xFFFF0000u) | 1u;
            return 1;
        case 0x48:                         // alloc -> фикт. сегмент
            r->eflags &= ~1u;
            r->eax = (r->eax & 0xFFFF0000u) | DOS_LOAD_SEG;
            return 1;
        case 0x49: case 0x4A:              // free / resize
            r->eflags &= ~1u;
            return 1;
        case 0x4D:                         // код возврата -> AX
            r->eax = (r->eax & 0xFFFF0000u) | (g_last_exit & 0xFF);
            return 1;
        case 0x4E: case 0x4F:              // FindFirst/Next -> нет файлов
            r->eflags |= 1u;
            r->eax = (r->eax & 0xFFFF0000u) | 18u;
            return 1;
        default:
            sputs("[mmdcm] int 21h AH="); sputh(ah); sputs(" (не реализовано)\n");
            return 1;
        }
    }
    if (n == 0x10) {
        switch (ah) {
        case 0x00: case 0x01: case 0x02:   // режим/курсор — игнор
            return 1;
        case 0x03:                         // позиция курсора
            r->eax = (r->eax & 0xFFFFFF00u);
            r->edx = 0; r->ecx = 0x0607;
            return 1;
        case 0x06: case 0x07:              // прокрутка
            return 1;
        case 0x08:                         // читать символ/атрибут
            r->eax = (r->eax & 0xFFFF0000u) | 0x0720u;
            return 1;
        case 0x09: case 0x0A:              // писать символ AL
            sputc((char)(r->eax & 0xFF));
            return 1;
        case 0x0E:                         // teletype
            sputc((char)(r->eax & 0xFF));
            return 1;
        case 0x0F:                         // режим -> 80x25 text
            r->eax = (r->eax & 0xFFFFFF00u) | 0x03u;
            r->ebx = (r->ebx & 0xFFFF0000u);
            return 1;
        default:
            return 1;
        }
    }
    if (n == 0x16) {                       // BIOS клавиатура
        unsigned char ah16 = (unsigned char)((r->eax >> 8) & 0xFF);
        if (ah16 == 0x00 || ah16 == 0x10) { r->eax = 0; return 1; }
        if (ah16 == 0x01 || ah16 == 0x11) { r->eflags |= 0x40u; r->eax = 0; return 1; }
        return 1;
    }
    if (n == 0x1A) {                       // BIOS время/таймер
        unsigned char ah1a = (unsigned char)((r->eax >> 8) & 0xFF);
        if (ah1a == 0x00) {
            static unsigned int ticks = 0;
            ticks += 0x100;
            r->ecx = (ticks >> 16) & 0xFFFF;
            r->edx = ticks & 0xFFFF;
            r->eax = (r->eax & 0xFFFFFF00u);
            return 1;
        }
        if (ah1a == 0x02) { r->ecx = 0; r->edx = 0; return 1; }
        if (ah1a == 0x04) { r->ecx = 1980; r->edx = (1u << 8) | 1u; return 1; }
        return 1;
    }
    sputs("[mmdcm] int 0x"); sputh((unsigned int)n); sputs(" (игнор)\n");
    return 1;
}

// ============================================================
// Обработчик исключений из V86 (вызывается из isr_handler).
// ============================================================
int mmdcm_handle_v86_exc(registers_t* r) {
    if (!g_v86_active) return 0;

    if (r->int_no == 13) {
        unsigned int lin = ((r->cs & 0xFFFFu) << 4) + (r->eip & 0xFFFFu);
        unsigned char* insn = (unsigned char*)lin;
        if (insn[0] == 0xCD) {            // int N
            int n = insn[1];
            int cont = emulate_int(n, r);
            if (cont) {
                // VME перехватывает int ДО выполнения: адреса возврата на
                // стеке V86 НЕТ. Просто пропускаем 'CD n'.
                r->eip = (r->eip + 2) & 0xFFFFu;
                gdt_set_esp0(g_v86_exc_stack_top);
                return 1;
            }
            g_v86_active = 0;
            g_v86_exited = 1;
            gdt_set_esp0(0x700000u);
            mmdcm_leave_v86();
            return 1;
        }
        // Не int — привилегированная инструкция (hlt/cli/...). Выход.
        sputs("[mmdcm] #GP в V86, opcode=0x"); sputh(insn[0]);
        sputs(" -> выход\n");
        set_err("privileged instruction in V86");
        g_v86_active = 0;
        g_v86_exited = 1;
        gdt_set_esp0(0x700000u);
        mmdcm_leave_v86();
        return 1;
    }

    sputs("[mmdcm] искл. в V86: int_no="); sputi((int)r->int_no);
    sputs(" eip="); sputh(r->eip);
    sputs(" cs="); sputh(r->cs); sputs("\n");
    set_err("exception in V86");
    g_v86_active = 0;
    g_v86_exited = 1;
    gdt_set_esp0(0x700000u);
    mmdcm_leave_v86();
    return 1;
}

// ============================================================
// DOS PSP (Program Segment Prefix), 256 байт по смещению 0 сегмента.
// ============================================================
static void build_psp(unsigned int seg) {
    unsigned int lin = dos_lin(seg, 0);
    if (lin + 256u > 0x100000u) return;
    unsigned char* psp = (unsigned char*)lin;
    for (int i = 0; i < 256; i++) psp[i] = 0;
    psp[0x00] = 0xCD; psp[0x01] = 0x20;      // int 20h (exit)
    psp[0x02] = 0xA0; psp[0x03] = 0x00;      // верхний адрес сегмента
    psp[0x50] = 0xCD; psp[0x51] = 0x21;      // int 21h
    psp[0x52] = 0xCB;                        // retf
    psp[0x80] = 0;                            // длина хвоста
    psp[0x81] = 0x0D;
}

// ============================================================
// Инициализация.
// ============================================================
void mmdcm_init(void) {
    sputs("[mmdcm] init, VME="); sputi(mmdcm_has_vme()); sputs("\n");
    if (!mmdcm_has_vme()) { sputs("[mmdcm] нет VME\n"); return; }

    unsigned int cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 1u;
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4));

    g_v86_exc_stack_top = (unsigned int)g_v86_exc_stack + sizeof(g_v86_exc_stack);

    // Блок физпамяти под DOS + PD для V86-задачи (PDE[0] -> dos_phys).
    unsigned int dos_phys = pmem_alloc_block();
    if (dos_phys) {
        g_dos_phys = dos_phys;
        g_v86_pd_phys = paging_v86_pd_alloc(dos_phys);
        sputs("[mmdcm] DOS phys="); sputh(dos_phys);
        sputs(" PD="); sputh(g_v86_pd_phys); sputs("\n");
    } else {
        sputs("[mmdcm] pmem_alloc_block failed, identity fallback\n");
    }

    // IVT + ловушки в низкой памяти (identity).
    unsigned char* mem = (unsigned char*)0x0;
    for (int i = 0; i < 256; i++) {
        unsigned short off = (unsigned short)(TRAP_BASE + i);
        mem[i*4 + 0] = (unsigned char)(off & 0xFF);
        mem[i*4 + 1] = (unsigned char)((off >> 8) & 0xFF);
        mem[i*4 + 2] = 0;
        mem[i*4 + 3] = 0;
        mem[TRAP_BASE + i] = 0xF4;   // hlt
    }
    sputs("[mmdcm] IVT + ловушки (hlt) установлены\n");
    g_last_error = 0;
    g_last_exit = 0;
}

// ============================================================
// Запуск V86-кода с явным размещением.
// ============================================================
int mmdcm_run_at(const unsigned char* code, unsigned int len,
                 unsigned int seg, unsigned int load_off) {
    if (!code || len == 0) { set_err("empty code"); return -1; }
    if (!mmdcm_has_vme()) { set_err("no VME support"); return -1; }
    if (!g_v86_exc_stack_top) { set_err("mmdcm not initialized"); return -1; }
    if (seg == 0) seg = DOS_LOAD_SEG;

    if ((unsigned int)load_off + len > 0x10000u) {
        set_err("program too large for segment");
        return -1;
    }
    unsigned int lin = dos_lin(seg, load_off);
    if (lin + len > 0x100000u) {
        set_err("program does not fit in low memory");
        return -1;
    }

    unsigned char* dst = (unsigned char*)lin;
    for (unsigned int i = 0; i < len; i++) dst[i] = code[i];

    sputs("[mmdcm] вход в V86 seg="); sputh(seg);
    sputs(" off="); sputh(load_off);
    sputs(" len="); sputi((int)len); sputs("\n");

    g_last_error = 0;
    g_last_exit = 0;
    g_v86_exited = 0;
    g_v86_active = 1;
    paging_low_user(1);
    gdt_set_esp0(g_v86_exc_stack_top);

    unsigned int cs = seg, ip = load_off;
    unsigned int ss = seg, sp = 0xFFFEu;
    unsigned int dseg = seg;

    mmdcm_enter_v86(0 /* g_v86_pd_phys: PD указывает на dos_phys, а код/IVT лежат в identity */, cs, ip, ss, sp, dseg);

    g_v86_active = 0;
    gdt_set_esp0(0x700000u);
    paging_low_user(0);
    sputs("[mmdcm] вернулись, exited="); sputi(g_v86_exited);
    sputs(" exit="); sputi((int)g_last_exit); sputs("\n");

    if (!g_v86_exited && !g_last_error)
        set_err("program did not exit cleanly");
    if (g_last_error) return -1;
    return (int)g_last_exit;
}

int mmdcm_run_code(const unsigned char* code, unsigned int len) {
    return mmdcm_run_at(code, len, DOS_LOAD_SEG, DOS_LOAD_OFF);
}

// ============================================================
// Загрузка .COM: PSP в начале сегмента, код по смещению 0x100.
// ============================================================
int mmdcm_load_com(const unsigned char* image, unsigned int len) {
    if (!image || len == 0) { set_err("empty COM"); return -1; }
    if (len > (0x10000u - DOS_LOAD_OFF)) { set_err("COM too large"); return -1; }
    build_psp(DOS_LOAD_SEG);
    sputs("[mmdcm] COM load len="); sputi((int)len); sputs("\n");
    return mmdcm_run_at(image, len, DOS_LOAD_SEG, DOS_LOAD_OFF);
}

// ============================================================
// Загрузка .EXE (MZ). Только без релокаций (reloc_count == 0).
// ============================================================
struct mz_header {
    unsigned short signature;      // 'MZ'
    unsigned short last_page;
    unsigned short pages;
    unsigned short reloc_count;
    unsigned short header_paras;
    unsigned short min_alloc;
    unsigned short max_alloc;
    unsigned short ss;
    unsigned short sp;
    unsigned short checksum;
    unsigned short ip;
    unsigned short cs;
    unsigned short reloc_off;
    unsigned short overlay;
} __attribute__((packed));

int mmdcm_load_exe(const unsigned char* image, unsigned int len) {
    if (!image || len < sizeof(struct mz_header)) { set_err("EXE too short"); return -1; }
    const struct mz_header* h = (const struct mz_header*)image;
    if (h->signature != 0x5A4D) { set_err("not an MZ EXE"); return -1; }
    if (h->reloc_count != 0) { set_err("EXE with relocations not supported"); return -1; }

    unsigned int header_bytes = (unsigned int)h->header_paras * 16u;
    if (header_bytes >= len) { set_err("bad EXE header size"); return -1; }
    unsigned int img_off = header_bytes;

    unsigned int img_size = (unsigned int)h->pages * 512u;
    if (h->last_page) img_size = (unsigned int)(h->pages - 1) * 512u + h->last_page;
    if (img_size == 0 || img_size > 0x10000u) { set_err("EXE size invalid"); return -1; }
    if (img_off + img_size > len) { set_err("truncated EXE"); return -1; }

    build_psp(DOS_LOAD_SEG);

    unsigned int cs = DOS_LOAD_SEG + h->cs;
    unsigned int ip = h->ip;
    unsigned int ss = DOS_LOAD_SEG + h->ss;
    unsigned int sp = h->sp;

    sputs("[mmdcm] EXE load img="); sputi((int)img_size);
    sputs(" cs:ip="); sputh(cs); sputs(":"); sputh(ip);
    sputs(" ss:sp="); sputh(ss); sputs(":"); sputh(sp); sputs("\n");

    unsigned int lin = dos_lin(DOS_LOAD_SEG, 0);
    if (lin + img_size > 0x100000u) { set_err("EXE does not fit low memory"); return -1; }
    unsigned char* dst = (unsigned char*)lin;
    for (unsigned int i = 0; i < img_size; i++) dst[i] = image[img_off + i];

    g_last_error = 0;
    g_last_exit = 0;
    g_v86_exited = 0;
    g_v86_active = 1;
    paging_low_user(1);
    gdt_set_esp0(g_v86_exc_stack_top);
    mmdcm_enter_v86(g_v86_pd_phys, cs, ip, ss, sp, DOS_LOAD_SEG);
    g_v86_active = 0;
    gdt_set_esp0(0x700000u);
    paging_low_user(0);

    sputs("[mmdcm] EXE exited="); sputi(g_v86_exited);
    sputs(" exit="); sputi((int)g_last_exit); sputs("\n");

    if (!g_v86_exited && !g_last_error) set_err("EXE did not exit cleanly");
    if (g_last_error) return -1;
    return (int)g_last_exit;
}

// ============================================================
// Запуск программы из файла ФС.
// ============================================================
#define MMDCM_MAX_IMAGE (64u * 1024u)
static unsigned char g_img_buf[MMDCM_MAX_IMAGE];

static int has_ext_ci(const char* path, const char* ext) {
    if (!path || !ext) return 0;
    int n = 0; while (path[n]) n++;
    int m = 0; while (ext[m]) m++;
    if (n < m) return 0;
    for (int i = 0; i < m; i++) {
        char a = path[n - m + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

int mmdcm_run_file(const char* path) {
    if (!path) { set_err("null path"); return -1; }
    int n = vfs_read_file(path, g_img_buf, MMDCM_MAX_IMAGE);
    if (n <= 0) { set_err("cannot read file"); return -1; }
    unsigned int len = (unsigned int)n;

    int is_exe = has_ext_ci(path, ".exe");
    if (!is_exe && len >= 2 && g_img_buf[0] == 'M' && g_img_buf[1] == 'Z')
        is_exe = 1;
    if (is_exe) return mmdcm_load_exe(g_img_buf, len);
    return mmdcm_load_com(g_img_buf, len);
}

unsigned int mmdcm_last_exit_code(void) { return g_last_exit; }
const char*  mmdcm_last_error(void) { return g_last_error; }

// ============================================================
// Тестовая DOS-программа: "Hello from DOS!" + код 42.
// Код грузится в seg 0x0200 (lin 0x2000); строка — по lin 0x2100.
// ============================================================
void mmdcm_test_dos(void) {
    unsigned char* msg = (unsigned char*)0x2100;
    const char* s = "Hello from DOS!$";
    int i = 0; while (s[i]) { msg[i] = (unsigned char)s[i]; i++; }
    static const unsigned char code[] = {
        0xBA, 0x00, 0x21,   // mov dx, 0x2100
        0xB4, 0x09,         // mov ah, 9
        0xCD, 0x21,         // int 21h
        0xB4, 0x4C,         // mov ah, 4Ch
        0xB0, 0x2A,         // mov al, 42
        0xCD, 0x21          // int 21h
    };
    int rc = mmdcm_run_at(code, sizeof(code), 0x0200, 0);
    sputs("[mmdcm] test_dos rc="); sputi(rc); sputs("\n");
}
