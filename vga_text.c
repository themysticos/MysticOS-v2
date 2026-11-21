// vga_text.c - переключение VGA в текстовый режим 80x25 (mode 3) через
// регистры, без BIOS. См. vga_text.h.
//
// Стандартные таблицы значений взяты из классического набора (как в
// FreeVGA / Linux vgacon / DOS mode 3). После записи регистров адаптер
// снова показывает текстовый буфер 0xB8000 — даже если BIOS/VBE оставили
// его в graphics mode.
#include "vga_text.h"
#include "vgadbg.h"
#include "io.h"

static int g_text_active = 0;

// --- стандартные регистровые наборы для mode 3 (80x25, 16 цветов) ---
static const unsigned char seq_regs[5] = {
    0x03, 0x00, 0x03, 0x00, 0x02
};
static const unsigned char crtc_regs[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};
static const unsigned char gc_regs[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
};
static const unsigned char ac_regs[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static void write_seq(int idx, unsigned char v) {
    outb(0x3C4, (unsigned char)idx);
    outb(0x3C5, v);
}
static void write_crtc(int idx, unsigned char v) {
    outb(0x3D4, (unsigned char)idx);
    outb(0x3D5, v);
}
static void write_gc(int idx, unsigned char v) {
    outb(0x3CE, (unsigned char)idx);
    outb(0x3CF, v);
}
static void write_ac(int idx, unsigned char v) {
    (void)inb(0x3DA);          // reset flip-flop
    outb(0x3C0, (unsigned char)idx);
    outb(0x3C0, v);
}

void vga_text_enter(void) {
    // 1) Misc Output Register: 0x67 = 25kHz, цветной, neg h/v sync.
    outb(0x3C2, 0x67);

    // 2) Sequencer: сброс, затем таблица, затем снять сброс.
    write_seq(0x00, 0x01);     // Synchronous reset
    for (int i = 0; i < 5; i++) write_seq(i, seq_regs[i]);
    write_seq(0x00, 0x03);     // снять reset

    // 3) Открыть доступ к CRTC (снять защиту регистров 0..7).
    write_crtc(0x11, 0x00);
    for (int i = 0; i < 25; i++) write_crtc(i, crtc_regs[i]);

    // 4) Graphics Controller.
    for (int i = 0; i < 9; i++) write_gc(i, gc_regs[i]);

    // 5) Attribute Controller: сброс flip-flop, таблица, включить видео.
    (void)inb(0x3DA);
    for (int i = 0; i < 21; i++) write_ac(i, ac_regs[i]);
    outb(0x3C0, 0x20);         // PAS = 1 (включить палитру/видео)

    // 6) Обнулить регистр адреса/курсора мы не трогаем — vgadbg сам ведёт
    //    позицию. Просто чистим экран в известное состояние.
    vga_clear();
    g_text_active = 1;
}

int vga_text_active(void) { return g_text_active; }

void safe_console_enter(const char* reason) {
    vga_text_enter();
    vga_set_attr(0x0F);        // ярко-белый на чёрном
    vga_print("=== MYSTICOS SAFE CONSOLE ===\n");
    vga_print("Reason: ");
    vga_print(reason ? reason : "unknown");
    vga_print("\n\n");
}
