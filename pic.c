// pic.c - PIC 8259, PIT, клавиатура (IRQ1), PS/2 мышь (IRQ12)
#include "pic.h"
#include "vbe.h"

void outb(unsigned short port, unsigned char v);
unsigned char inb(unsigned short port);

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20
#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PS2_DATA  0x60
#define PS2_STAT  0x64

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static volatile unsigned int g_ticks = 0;

#define KB_SIZE 64
// Буфер клавиатурных СОБЫТИЙ: (pressed<<8) | код. Так храним и нажатия,
// и ОТПУСКАНИЯ (нужно Doom: иначе движение «залипает»).
static volatile unsigned short kb_buf[KB_SIZE];
static volatile unsigned int kb_head = 0, kb_tail = 0;
static int shift = 0, caps = 0;
static int e0_prefix = 0;   // префикс 0xE0 перед расширенными клавишами (стрелки)

static const char sc_lower[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

static void kb_push(char c) {
    unsigned int n = (kb_head + 1) % KB_SIZE;
    if (n != kb_tail) { kb_buf[kb_head] = c; kb_head = n; }
}
int kb_available(void) { return kb_head != kb_tail; }
int kb_getchar(void) {
    while (kb_head == kb_tail) { __asm__ volatile ("hlt"); }
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_SIZE;
    return c;
}

// Очередь СОБЫТИЙ клавиш (raw scancode + нажатие/отпускание) - для игр.
static volatile unsigned short ev_buf[KB_SIZE];
static volatile unsigned int ev_head = 0, ev_tail = 0;

void kb_push_event(int pressed, unsigned char sc) {
    unsigned int n = (ev_head + 1) % KB_SIZE;
    unsigned short ev = (unsigned short)(((pressed ? 1u : 0u) << 8) | sc);
    if (n != ev_tail) { ev_buf[ev_head] = ev; ev_head = n; }
}
int kb_event_available(void) { return ev_head != ev_tail; }
// Возвращает (pressed<<8)|sc или -1 если пусто (неблокирующе).
int kb_get_event(void) {
    if (ev_head == ev_tail) return -1;
    unsigned short ev = ev_buf[ev_tail];
    ev_tail = (ev_tail + 1) % KB_SIZE;
    return (int)ev;
}

static int m_x = 400, m_y = 300;
static int m_left = 0, m_right = 0;
// Относительное движение (для игр, mouse grab): накапливается без границ.
static int g_rel_x = 0, g_rel_y = 0;
static int g_mouse_grab = 0;
void mouse_set_grab(int on) {
    // Сбрасываем дельту ТОЛЬКО при переходе 0->1 (вход в игру), иначе
    // при вызове каждый кадр дельта обнулялась бы постоянно.
    if (on && !g_mouse_grab) { g_rel_x = g_rel_y = 0; }
    g_mouse_grab = on;
}
// Забрать накопленную относительную дельту (обнуляет).
void mouse_take_rel(int* dx, int* dy) { *dx = g_rel_x; *dy = g_rel_y; g_rel_x = 0; g_rel_y = 0; }
int mouse_grabbed(void) { return g_mouse_grab; }
static unsigned char mouse_cycle = 0;
static unsigned char mouse_packet[3];
static void (*g_move_hook)(int dx, int dy) = 0;

void mouse_set_move_hook(void (*hook)(int dx, int dy)) { g_move_hook = hook; }

int mouse_x(void) { return m_x; }
int mouse_y(void) { return m_y; }
int mouse_left(void) { return m_left; }
int mouse_right(void) { return m_right; }

static void mouse_wait_write(void) { int t = 100000; while (t-- && (inb(PS2_STAT) & 2)); }
static void mouse_wait_read(void)  { int t = 100000; while (t-- && !(inb(PS2_STAT) & 1)); }
static void mouse_write(unsigned char v) {
    mouse_wait_write(); outb(PS2_STAT, 0xD4);
    mouse_wait_write(); outb(PS2_DATA, v);
}
static unsigned char mouse_read(void) { mouse_wait_read(); return inb(PS2_DATA); }

void mouse_init(void) {
    mouse_wait_write(); outb(PS2_STAT, 0xA8);
    mouse_wait_write(); outb(PS2_STAT, 0x20);
    mouse_wait_read();
    unsigned char status = inb(PS2_DATA);
    status |= 0x02; status &= ~0x20;
    mouse_wait_write(); outb(PS2_STAT, 0x60);
    mouse_wait_write(); outb(PS2_DATA, status);
    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();
}

void irq_handler(registers_t* r) {
    unsigned int irq = r->int_no - 0x20;
    if (irq == 0) { g_ticks++; }
    else if (irq == 1) {
        // Читаем, ПОКА есть байты, и ТОЛЬКО клавиатурные (статус бит 5 = 0).
        // Иначе при удержании клавиши (auto-repeat) захватим байт мыши -> курсор слетает.
        while (inb(PS2_STAT) & 0x01) {
        if (inb(PS2_STAT) & 0x20) break;   // это байт мыши - оставим IRQ12
        unsigned char sc = inb(PS2_DATA);
        // Расширенный префикс (0xE0) — следом идёт код стрелок/Delete/Home/End.
        if (sc == 0xE0) { e0_prefix = 1; continue; }
        int pressed = !(sc & 0x80);
        unsigned char code = sc & 0x7F;
        if (e0_prefix) {
            e0_prefix = 0;
            kb_push_event(pressed, (unsigned char)(0x80 | code));
            if (pressed) kb_push((char)(0x80 | code));
            continue;
        }
        kb_push_event(pressed, code);
        if (sc == 0x2A || sc == 0x36) shift = 1;
        else if (sc == 0xAA || sc == 0xB6) shift = 0;
        else if (sc == 0x3A) caps = !caps;
        else if (sc == 0x1D) kb_push((char)0x01);
        else if (sc == 0x9D) { /* release Ctrl */ }
        else if (sc == 0x3C) kb_push((char)0x02);
        else if (pressed && sc < 128) {
            char c = sc_lower[sc];
            if (c >= 'a' && c <= 'z' && (shift ^ caps)) c -= 32;
            if (c) kb_push(c);
        }
        }   // end while (клавиатурные байты)
    } else if (irq == 12) {
        // Читаем ВСЕ байты, что накопились, с корректной ресинхронизацией.
        while (inb(PS2_STAT) & 0x01) {
            if (!(inb(PS2_STAT) & 0x20)) break;   // это байт клавиатуры - оставим IRQ1
            unsigned char data = inb(PS2_DATA);
            if (mouse_cycle == 0) {
                if (!(data & 0x08)) continue;
                mouse_packet[0] = data;
                mouse_cycle = 1;
            } else if (mouse_cycle == 1) {
                mouse_packet[1] = data;
                mouse_cycle = 2;
            } else {
                mouse_packet[2] = data;
                mouse_cycle = 0;
                unsigned char flags = mouse_packet[0];
                int dx = (int)mouse_packet[1];
                int dy = (int)mouse_packet[2];
                if (flags & 0x10) dx |= 0xFFFFFF00;
                if (flags & 0x20) dy |= 0xFFFFFF00;
                if (!(flags & 0xC0)) {
                    // Дельта копится ВСЕГДА (для игр - свободный разворот).
                    g_rel_x += dx; g_rel_y -= dy;
                    if (!g_mouse_grab) {
                        m_x += dx; m_y -= dy;
                        if (m_x < 0) m_x = 0;
                        if (m_y < 0) m_y = 0;
                        if (m_x > (int)vbe_width() - 1)  m_x = (int)vbe_width() - 1;
                        if (m_y > (int)vbe_height() - 1) m_y = (int)vbe_height() - 1;
                        if (g_move_hook) g_move_hook(dx, -dy);
                    }
                }
                m_left = flags & 0x01; m_right = flags & 0x02;
            }
        }
    }
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_remap(void) {
    outb(PIC1_CMD, 0x11); outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20); outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04); outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01); outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xEF);
}

void pit_init(unsigned int hz) {
    unsigned int div = 1193182 / hz;
    if (div > 65535) div = 65535;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (unsigned char)(div & 0xFF));
    outb(PIT_CH0, (unsigned char)((div >> 8) & 0xFF));
}

void pic_init(void) {
    pic_remap();
    idt_set_gate_extern(0x20, (unsigned int)irq0);
    idt_set_gate_extern(0x21, (unsigned int)irq1);
    idt_set_gate_extern(0x22, (unsigned int)irq2);
    idt_set_gate_extern(0x23, (unsigned int)irq3);
    idt_set_gate_extern(0x24, (unsigned int)irq4);
    idt_set_gate_extern(0x25, (unsigned int)irq5);
    idt_set_gate_extern(0x26, (unsigned int)irq6);
    idt_set_gate_extern(0x27, (unsigned int)irq7);
    idt_set_gate_extern(0x28, (unsigned int)irq8);
    idt_set_gate_extern(0x29, (unsigned int)irq9);
    idt_set_gate_extern(0x2A, (unsigned int)irq10);
    idt_set_gate_extern(0x2B, (unsigned int)irq11);
    idt_set_gate_extern(0x2C, (unsigned int)irq12);
    idt_set_gate_extern(0x2D, (unsigned int)irq13);
    idt_set_gate_extern(0x2E, (unsigned int)irq14);
    idt_set_gate_extern(0x2F, (unsigned int)irq15);
}

void interrupts_enable(void) { __asm__ volatile ("sti"); }
unsigned int timer_ticks(void) { return g_ticks; }
