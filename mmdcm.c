// ============================================================================
// mmdcm.c - MMDCM: MysticOS MS-DOS Compatibility Module
// ============================================================================
//
//   тут запускаются DOS-программы. я до сих пор не уверен, что понимаю,
//   ПОЧЕМУ это работает, но оно работает, и я не трогаю.
//
// --- что тут вообще происходит ---
//
// V86 (Virtual 8086) — режим, где процессор сам исполняет 16-битный код
// как в 1985. Мы влезаем в прерывания и делаем вид, что мы DOS.
//
// КАК ЛОВИМ ПРЕРЫВАНИЯ — я честно хз почему именно так, но:
// VME (CR4.VME=1) даёт #GP(13) прямо на инструкции 'CD N' (int N).
// Почему? Потому что IDT-гейт DPL=0, а V86 это CPL=3. Наверное. Магия.
// ВАЖНО: адрес возврата НЕ кладётся на стек! Просто eip += 2. ВСЁ.
// Я убил на поиск этого целую ночь. Не повторяйте.
// ============================================================================
#include "mmdcm.h"
#include "serial.h"
#include "pmem.h"
#include "paging.h"
#include "idt.h"   // registers_t
#include "gdt.h"   // gdt_set_esp0

extern void mmdcm_enter_v86(unsigned int pd_phys, unsigned int cs, unsigned int ip,
                            unsigned int ss, unsigned int sp, unsigned int ds_es);
extern void mmdcm_leave_v86(void);

// Отдельный стек для исключений из V86 (НЕ стек ядра 0x90000!).
static unsigned char g_v86_exc_stack[16384] __attribute__((aligned(16)));
static unsigned int  g_v86_exc_stack_top = 0;

int g_v86_active = 0;               // 1 = исполняется V86-код
static volatile int g_v86_exited = 0;

#define TRAP_BASE  0x3000          // адрес ловушек (0x3000 + N)

int mmdcm_has_vme(void) {
    unsigned int eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    return (ecx & 1u) ? 1 : 0;
}

static void sputc(char c) { char s[2] = { c, 0 }; serial_print(s); }

int mmdcm_run_code(const unsigned char* code, unsigned int len);   // forward

// ------------------------------------------------------------------
// TODO: разобраться, почему это вообще работает. (я пробовал понять.
// стало только хуже. оставил как есть, оно живое.)
// ------------------------------------------------------------------

// ============================================================
// Эмуляция DOS/BIOS.
//
//   тут мы притворяемся старым добрым DOS. int 21h -> мы DOS,
//   int 10h -> мы BIOS. программы верят. я тоже почти поверил.
//
// regs — регистры V86 (кадр). 1 = вернуться в V86, 0 = выход.
// ============================================================
static int emulate_int(int n, registers_t* r) {
    unsigned char ah = (unsigned char)((r->eax >> 8) & 0xFF);

    if (n == 0x20) {                       // DOS: завершить программу
        serial_print("[mmdcm] int 20h -> exit\n");
        return 0;
    }
    if (n == 0x21) {
        if (ah == 0x4C) {
            serial_print("[mmdcm] int 21h:4C -> exit\n");
            return 0;
        }
        if (ah == 0x02) {                  // вывод символа DL
            sputc((char)(r->edx & 0xFF));
            return 1;
        }
        if (ah == 0x09) {                  // вывод строки DS:DX ($-конец)
            unsigned int seg = r->ds & 0xFFFF;
            unsigned int off = r->edx & 0xFFFF;
            const char* p = (const char*)((seg << 4) + off);
            int guard = 0;
            while (*p != '$' && guard++ < 4096) sputc(*p++);
            return 1;
        }
        serial_print("[mmdcm] int 21h AH=");
        serial_print_hex(ah);
        serial_print(" (не реализовано)\n");
        return 1;
    }
    if (n == 0x10) {
        if (ah == 0x0E) {                  // teletype: вывод символа AL
            sputc((char)(r->eax & 0xFF));
            return 1;
        }
        return 1;
    }
    serial_print("[mmdcm] int 0x");
    serial_print_hex((unsigned int)n);
    serial_print(" (игнор)\n");
    return 1;
}

// ============================================================
// Обработчик исключений из V86 (вызывается из isr_handler).
// ============================================================
int mmdcm_handle_v86_exc(registers_t* r) {
    if (!g_v86_active) return 0;

    // тут ловим int. VME даёт #GP прямо на инструкции 'CD n',
    // eip указывает ровно на неё. читаем байт n, пропускаем 2 байта.
    //
    //   Я НЕ ЗНАЮ ПОЧЕМУ АДРЕС ВОЗВРАТА НЕ КЛАДЁТСЯ НА СТЕК.
    //   Серьёзно. Должен бы. Не кладётся. eip+=2 и всё работает.
    //   Если однажды сломается — ну, ты предупреждён.
    if (r->int_no == 13) {
        unsigned int lin = ((r->cs & 0xFFFFu) << 4) + (r->eip & 0xFFFFu);
        unsigned char* insn = (unsigned char*)lin;
        if (insn[0] == 0xCD) {            // int N
            int n = insn[1];
            int cont = emulate_int(n, r);
            if (cont) {
                // VME перехватывает int ДО его выполнения: адрес возврата
                // НЕ кладётся на стек V86. Просто пропускаем 'CD n'.
                r->eip = (r->eip + 2) & 0xFFFFu;
                gdt_set_esp0(g_v86_exc_stack_top);
                return 1;   // iret -> продолжим в V86 после int
            }
            g_v86_active = 0;
            g_v86_exited = 1;
            gdt_set_esp0(0x90000u);
            mmdcm_leave_v86();
            return 1;
        }
        // Не int — привилегированная инструкция (hlt/cli/...). Выход.
        serial_print("[mmdcm] #GP в V86, opcode=0x");
        serial_print_hex(insn[0]);
        serial_print(" -> выход\n");
        g_v86_active = 0;
        g_v86_exited = 1;
        gdt_set_esp0(0x90000u);
        mmdcm_leave_v86();
        return 1;
    }

    serial_print("[mmdcm] искл. в V86: int_no=");
    serial_print_int((int)r->int_no);
    serial_print(" eip=");
    serial_print_hex(r->eip);
    serial_print(" cs=");
    serial_print_hex(r->cs);
    serial_print("\n");
    g_v86_active = 0;
    g_v86_exited = 1;
    gdt_set_esp0(0x90000u);
    mmdcm_leave_v86();
    return 1;
}

// ============================================================
// Инициализация.
// ============================================================
void mmdcm_init(void) {
    serial_print("[mmdcm] init, VME=");
    serial_print_int(mmdcm_has_vme());
    serial_print("\n");
    if (!mmdcm_has_vme()) { serial_print("[mmdcm] нет VME\n"); return; }

    unsigned int cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 1u;
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4));

    g_v86_exc_stack_top = (unsigned int)g_v86_exc_stack + sizeof(g_v86_exc_stack);

    unsigned int dos_phys = pmem_alloc_block();
    if (dos_phys) paging_v86_pd_alloc(dos_phys);

    // --- IVT + ловушки в НИЗКОЙ памяти (identity). ---
    unsigned char* mem = (unsigned char*)0x0;
    for (int i = 0; i < 256; i++) {
        unsigned short off = (unsigned short)(TRAP_BASE + i);
        mem[i*4 + 0] = (unsigned char)(off & 0xFF);
        mem[i*4 + 1] = (unsigned char)((off >> 8) & 0xFF);
        mem[i*4 + 2] = 0;
        mem[i*4 + 3] = 0;
        mem[TRAP_BASE + i] = 0xF4;   // hlt
    }
    serial_print("[mmdcm] IVT + ловушки (hlt) установлены\n");
}

// ============================================================
// Запуск V86-кода (из буфера). Пока — identity (ядро видимо).
// ============================================================
// Тестовая DOS-программа: вывод строки через int 21h:9, выход int 21h:4C.
void mmdcm_test_dos(void) {
    // Строка по адресу 0x2100.
    unsigned char* msg = (unsigned char*)0x2100;
    const char* s = "Hello from DOS!$";
    int i = 0; while (s[i]) { msg[i] = (unsigned char)s[i]; i++; }
    // Код по 0x2000: mov dx,0x2100; mov ah,9; int 21h; mov ah,4Ch; int 21h
    static const unsigned char code[] = {
        0xBA, 0x00, 0x21,   // mov dx, 0x2100
        0xB4, 0x09,         // mov ah, 9
        0xCD, 0x21,         // int 21h
        0xB4, 0x4C,         // mov ah, 4Ch
        0xCD, 0x21          // int 21h
    };
    mmdcm_run_code(code, sizeof(code));
}

int mmdcm_run_code(const unsigned char* code, unsigned int len) {
    unsigned char* low = (unsigned char*)0x2000;
    for (unsigned int i = 0; i < len && i < 0x0800; i++) low[i] = code[i];

    serial_print("[mmdcm] вход в V86 (код @0x2000)...\n");
    g_v86_exited = 0;
    g_v86_active = 1;
    paging_low_user(1);
    gdt_set_esp0(g_v86_exc_stack_top);

    mmdcm_enter_v86(0, 0, 0x2000, 0, 0x7C00, 0);

    g_v86_active = 0;
    gdt_set_esp0(0x90000u);
    paging_low_user(0);
    serial_print("[mmdcm] вернулись, exited=");
    serial_print_int(g_v86_exited);
    serial_print("\n");
    return 0;
}
