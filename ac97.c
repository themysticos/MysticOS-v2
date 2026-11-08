// ac97.c - Intel AC'97 audio controller (Native Audio Mixer + Bus Master).
//
// PCI: class 0x04, subclass 0x01.
// BAR0 = NAM  (Native Audio Mixer, codec registers, 16-bit)
// BAR1 = NABM (Native Audio Bus Master, PCM out DMA)
//
// В QEMU AC97 обычно отдаётся как I/O-port BAR'ы, поэтому здесь поддержаны
// и port I/O, и MMIO (на случай реального железа).
//
// Поток: 16-бит stereo 48 кГц, кольцо из 32 BDLE. Позицию читаем из
// CIV/PICB. Музыку берём из music_next_sample() (моно 48к), как и HDA.
#include "ac97.h"
#include "vss.h"
#include "driver.h"
#include "pci.h"
#include "serial.h"
#include "pmem.h"
#include "music.h"

// ---- ring / BDL ----------------------------------------------------------
#define AC97_RING_FRAMES   32768u
#define AC97_RING_SAMPLES  (AC97_RING_FRAMES * 2u)
#define AC97_BDL_ENTRIES   32
#define AC97_BD_FRAMES     (AC97_RING_FRAMES / AC97_BDL_ENTRIES)
#define AC97_BD_SAMPLES    (AC97_BD_FRAMES * 2u)
#define AC97_MARGIN        4096u

// ---- NAM (BAR0) codec registers -----------------------------------------
#define AC97_RESET         0x00
#define AC97_MASTER_VOL    0x02
#define AC97_PCM_OUT_VOL   0x18

// ---- NABM (BAR1) registers ----------------------------------------------
#define NABM_PO_BDBAR      0x10
#define NABM_PO_CIV        0x14
#define NABM_PO_LVI        0x15
#define NABM_PO_SR         0x16
#define NABM_PO_PICB       0x18
#define NABM_PO_CR         0x1B
#define NABM_GLOB_CNT      0x2C
#define NABM_GLOB_STS      0x30

#define CR_RPBM  0x01
#define CR_RR    0x02

struct ac97_bdle {
    unsigned int  addr;
    unsigned short len;
    unsigned short flags;
} __attribute__((packed));

static struct ac97_bdle g_bdl[AC97_BDL_ENTRIES] __attribute__((aligned(8)));

static short* g_ring = 0;
static unsigned int g_ring_wr = 0;
static int g_ready = 0;
static volatile int g_in_mix = 0;

static unsigned short g_nam_port = 0, g_nabm_port = 0;
static volatile unsigned char *g_nam_mmio = 0, *g_nabm_mmio = 0;
static int g_nam_io = 0, g_nabm_io = 0;

static inline void outb_(unsigned short p, unsigned char v)  { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw_(unsigned short p, unsigned short v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outl_(unsigned short p, unsigned int v)   { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline unsigned char  inb_(unsigned short p) { unsigned char v;  __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline unsigned short inw_(unsigned short p) { unsigned short v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline unsigned int   inl_(unsigned short p) { unsigned int v;   __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }

static void udelay_loop(unsigned int n) { for (volatile unsigned int i = 0; i < n; i++) {} }

static void nam_w16(unsigned int off, unsigned short v) {
    if (g_nam_io) outw_(g_nam_port + off, v);
    else *(volatile unsigned short*)(g_nam_mmio + off) = v;
}

static unsigned char nabm_r8(unsigned int off) {
    if (g_nabm_io) return inb_(g_nabm_port + off);
    return *(volatile unsigned char*)(g_nabm_mmio + off);
}
static unsigned short nabm_r16(unsigned int off) {
    if (g_nabm_io) return inw_(g_nabm_port + off);
    return *(volatile unsigned short*)(g_nabm_mmio + off);
}
static unsigned int nabm_r32(unsigned int off) {
    if (g_nabm_io) return inl_(g_nabm_port + off);
    return *(volatile unsigned int*)(g_nabm_mmio + off);
}
static void nabm_w8(unsigned int off, unsigned char v) {
    if (g_nabm_io) outb_(g_nabm_port + off, v);
    else *(volatile unsigned char*)(g_nabm_mmio + off) = v;
}
static void nabm_w16(unsigned int off, unsigned short v) {
    if (g_nabm_io) outw_(g_nabm_port + off, v);
    else *(volatile unsigned short*)(g_nabm_mmio + off) = v;
}
static void nabm_w32(unsigned int off, unsigned int v) {
    if (g_nabm_io) outl_(g_nabm_port + off, v);
    else *(volatile unsigned int*)(g_nabm_mmio + off) = v;
}

static int ac97_init(void) {
    unsigned int gc = nabm_r32(NABM_GLOB_CNT);
    nabm_w32(NABM_GLOB_CNT, gc & ~0x2u);
    udelay_loop(100000);
    nabm_w32(NABM_GLOB_CNT, (gc & ~0x2u) | 0x2u);
    udelay_loop(100000);

    nam_w16(AC97_RESET, 0x0000);
    udelay_loop(100000);

    nam_w16(AC97_MASTER_VOL,  0x0000);
    nam_w16(AC97_PCM_OUT_VOL, 0x0808);

    g_ring = (short*)pmem_alloc_block();
    if (!g_ring) { serial_print("[ac97] no pmem\n"); return 0; }
    for (unsigned int i = 0; i < AC97_RING_SAMPLES; i++) g_ring[i] = 0;

    for (unsigned int i = 0; i < AC97_BDL_ENTRIES; i++) {
        g_bdl[i].addr  = (unsigned int)(g_ring + i * AC97_BD_SAMPLES);
        g_bdl[i].len   = (unsigned short)(AC97_BD_SAMPLES - 1u);
        g_bdl[i].flags = 0x0000;
    }

    nabm_w8(NABM_PO_CR, CR_RR);
    udelay_loop(10000);

    nabm_w32(NABM_PO_BDBAR, (unsigned int)g_bdl);
    nabm_w8 (NABM_PO_LVI,   AC97_BDL_ENTRIES - 1u);
    nabm_w16(NABM_PO_SR,    0xFFFF);
    nabm_w8 (NABM_PO_CR,    CR_RPBM);

    g_ring_wr = 0;
    g_ready = 1;
    serial_print("[ac97] ready, BDL=32x1024 frames, 48k stereo\n");
    return 1;
}

static unsigned int ac97_hw_read_frame(void) {
    unsigned char civ = nabm_r8(NABM_PO_CIV) % AC97_BDL_ENTRIES;
    unsigned short picb = nabm_r16(NABM_PO_PICB);
    unsigned int left = picb / 2u;
    if (left > AC97_BD_FRAMES) left = AC97_BD_FRAMES;
    unsigned int rd = civ * AC97_BD_FRAMES + (AC97_BD_FRAMES - left);
    return rd % AC97_RING_FRAMES;
}

static void ac97_mix(unsigned int count) {
    for (unsigned int i = 0; i < count; i++) {
        int s = music_next_sample();
        g_ring[g_ring_wr * 2]     = (short)s;
        g_ring[g_ring_wr * 2 + 1] = (short)s;
        g_ring_wr = (g_ring_wr + 1u) % AC97_RING_FRAMES;
    }
}

static void ac97_poll(void) {
    if (!g_ready) return;
    if (g_in_mix) return;
    g_in_mix = 1;

    unsigned int rd = ac97_hw_read_frame();
    unsigned int fill = (g_ring_wr + AC97_RING_FRAMES - rd) % AC97_RING_FRAMES;

    if (fill >= AC97_RING_FRAMES - 1u) {
        g_ring_wr = rd;
        fill = 0;
    }

    {
        static unsigned int dbg = 0;
        if ((++dbg % 200u) == 0u) {
            serial_print("[ac97] civ="); serial_print_int((int)nabm_r8(NABM_PO_CIV));
            serial_print(" picb="); serial_print_int((int)nabm_r16(NABM_PO_PICB));
            serial_print(" sr="); serial_print_hex(nabm_r16(NABM_PO_SR));
            serial_print(" cr="); serial_print_hex(nabm_r8(NABM_PO_CR));
            serial_print(" rd="); serial_print_int((int)rd);
            serial_print(" fill="); serial_print_int((int)fill);
            serial_print(" gsts="); serial_print_hex(nabm_r32(NABM_GLOB_STS));
            serial_print(" bdbar="); serial_print_hex(nabm_r32(NABM_PO_BDBAR));
            serial_print(" lvi="); serial_print_int((int)nabm_r8(NABM_PO_LVI));
            serial_print(" gcnt="); serial_print_hex(nabm_r32(NABM_GLOB_CNT));
            serial_print("\n");
        }
    }
    unsigned int target = AC97_RING_FRAMES - AC97_MARGIN;
    if (fill < target) {
        unsigned int need = target - fill;
        if (need > AC97_RING_FRAMES / 2u) need = AC97_RING_FRAMES / 2u;
        ac97_mix(need);
    }

    g_in_mix = 0;
}

static int ac97_vss_probe(void) { return g_ready; }
static int ac97_ready(void)     { return g_ready; }
static void ac97_set_rate(unsigned int rate) { (void)rate; }
static void ac97_play_pcm(const unsigned char* buf, unsigned int len, unsigned int rate) {
    (void)buf; (void)len; (void)rate;
}
static void ac97_beep(unsigned int freq_hz, unsigned int ms) { (void)freq_hz; (void)ms; }

static const sound_driver_t AC97_SOUND_DRIVER = {
    "ac97",
    ac97_vss_probe,
    ac97_ready,
    ac97_set_rate,
    ac97_play_pcm,
    ac97_beep,
    ac97_poll
};

static int ac97_pci_match(const device_t* dev) {
    if ((dev->class_code >> 8) != 0x04) return 0;
    if ((dev->class_code & 0xFF) != 0x01) return 0;
    return 1;
}

static int ac97_pci_probe(device_t* dev) {
    unsigned int bar0 = dev->bar[0];
    unsigned int bar1 = dev->bar[1];
    if (!bar0 || !bar1) return -1;

    unsigned short cmd = pci_read16(dev->bus_num, dev->slot, dev->func, 0x04);
    cmd |= 0x0007;
    pci_write32(dev->bus_num, dev->slot, dev->func, 0x04, cmd);

    // driver.c теряет флаг is_io (маскирует младшие биты), поэтому решаем
    // по величине: I/O-порт всегда < 0x10000. QEMU AC97 — это порты.
    if (bar0 < 0x10000u) { g_nam_io = 1; g_nam_port = (unsigned short)(bar0 & ~3u); }
    else                 { g_nam_mmio = (volatile unsigned char*)(bar0 & ~0xFu); }
    if (bar1 < 0x10000u) { g_nabm_io = 1; g_nabm_port = (unsigned short)(bar1 & ~3u); }
    else                 { g_nabm_mmio = (volatile unsigned char*)(bar1 & ~0xFu); }

    serial_print("[ac97] found, NAM="); serial_print_hex(bar0);
    serial_print(" NABM="); serial_print_hex(bar1);
    serial_print(g_nam_io ? " (io)\n" : " (mmio)\n");

    if (!ac97_init()) return -1;
    vss_register(&AC97_SOUND_DRIVER);
    dev->priv = (void*)1;
    return 0;
}

static const driver_t AC97_DRIVER = {
    "ac97",
    DRV_AUDIO,
    DRV_LEVEL_NORMAL,
    ac97_pci_match,
    ac97_pci_probe,
    0
};

void ac97_driver_register(void) {
    driver_register(&AC97_DRIVER);
}
