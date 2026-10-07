// sb16.c - Sound Blaster 16 драйвер MysticOS (PCM-вывод через 8-бит DMA).
// QEMU: -device sb16. База 0x220, IRQ5, 8-бит DMA канал 1.
#include "sb16.h"
#include "io.h"

#define SB_BASE    0x220
#define DSP_RESET  (SB_BASE + 6)
#define DSP_READ   (SB_BASE + 0xA)
#define DSP_WRITE  (SB_BASE + 0xC)
#define DSP_WSTAT  (SB_BASE + 0xC)
#define DSP_RSTAT  (SB_BASE + 0xE)
#define MIX_ADDR   (SB_BASE + 4)
#define MIX_DATA   (SB_BASE + 5)

static int g_ready = 0;
int sb16_ready(void) { return g_ready; }

static void dly(void) { for (volatile int i = 0; i < 1000; i++) {} }

static void dsp_write(unsigned char v) {
    int t = 100000; while (t-- && (inb(DSP_WSTAT) & 0x80)) {}
    outb(DSP_WRITE, v);
}
static unsigned char dsp_read(void) {
    int t = 100000; while (t-- && !(inb(DSP_RSTAT) & 0x80)) {}
    return inb(DSP_READ);
}
static void mixer_write(unsigned char reg, unsigned char val) {
    outb(MIX_ADDR, reg); dly(); outb(MIX_DATA, val); dly();
}

// DMA-буфер в .bss (identity-адрес < 16 МБ - требование 8237 ISA DMA).
static unsigned char g_dma_buf[32768] __attribute__((aligned(4096)));

// Программирование 8237 DMA, 8-бит канал 1 (память -> устройство).
static void dma_setup(unsigned int phys, unsigned int len) {
    outb(0x0A, 0x05);                       // маска канал 1
    outb(0x0C, 0x00);                       // сброс flip-flop
    // SINGLE (0x45): буфер играется ОДИН раз и стоп. Для разовых звуков.
    outb(0x0B, 0x45);
    outb(0x02, (unsigned char)(phys & 0xFF));
    outb(0x02, (unsigned char)((phys >> 8) & 0xFF));
    outb(0x83, (unsigned char)((phys >> 16) & 0xFF));
    outb(0x03, (unsigned char)((len - 1) & 0xFF));
    outb(0x03, (unsigned char)(((len - 1) >> 8) & 0xFF));
    outb(0x0A, 0x01);                       // снять маску канал 1
}

void sb16_set_rate(unsigned int rate) {
    if (!g_ready) return;
    dsp_write(0x41);                        // SB16: set output sample rate
    dsp_write((unsigned char)((rate >> 8) & 0xFF));
    dsp_write((unsigned char)(rate & 0xFF));
}

void sb16_play_pcm(const unsigned char* buf, unsigned int len, unsigned int rate) {
    if (!g_ready || len == 0) return;
    if (len > sizeof(g_dma_buf)) len = sizeof(g_dma_buf);
    for (unsigned int i = 0; i < len; i++) g_dma_buf[i] = buf[i];
    sb16_set_rate(rate);
    dma_setup((unsigned int)g_dma_buf, len);
    // Linux (sb16_main.c): 8-бит вывод = 0xC6, ПОТОМ ФОРМАТ, потом длина-1.
    // 0x14 = single-cycle 8-bit output (один раз, без format-байта).
    dsp_write(0x14);
    dsp_write((unsigned char)((len - 1) & 0xFF));
    dsp_write((unsigned char)(((len - 1) >> 8) & 0xFF));
}

int sb16_init(void) {
    // Сброс DSP.
    outb(DSP_RESET, 1); dly(); dly(); dly();
    outb(DSP_RESET, 0);
    unsigned char r = 0;
    int t = 1000000;
    while (t--) { if (inb(DSP_RSTAT) & 0x80) { r = inb(DSP_READ); break; } }
    if (r != 0xAA) return 0;
    dsp_write(0xD1);                        // включить динамик
    mixer_write(0x00, 0xCC);                // master volume max
    mixer_write(0x04, 0xCC);                // voice (DAC) max
    g_ready = 1;
    return 1;
}

// Простой системный тон: прямоугольник -> PCM 8-бит беззнаковый.
void sb16_beep(unsigned int freq_hz, unsigned int ms) {
    if (!g_ready || freq_hz == 0) return;
    unsigned int rate = 22050;
    unsigned int n = rate * ms / 1000;
    if (n > sizeof(g_dma_buf)) n = sizeof(g_dma_buf);
    unsigned int period = rate / freq_hz; if (period < 2) period = 2;
    for (unsigned int i = 0; i < n; i++)
        g_dma_buf[i] = ((i / (period / 2)) & 1) ? 0xD0 : 0x30;
    sb16_set_rate(rate);
    dma_setup((unsigned int)g_dma_buf, n);
    // 0x14 = single-cycle 8-bit output (играет ОДИН раз, без format-байта).
    dsp_write(0x14);
    dsp_write((unsigned char)((n - 1) & 0xFF));
    dsp_write((unsigned char)(((n - 1) >> 8) & 0xFF));
}

void sb16_poll(void) { /* позже: refill под музыку/игры */ }
