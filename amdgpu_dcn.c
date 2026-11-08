// amdgpu_dcn.c - минимальный DCN 1.0 драйвер для MysticOS (Vega/Raven).
//
// ЭТАП 2: инициализация одного display-pipe и заливка экрана цветом.
//
// ОСНОВА: Linux display/dc (dcn10_hubp/optc/opp/dpp), vega10_ip_offset.h.
// Формула адреса регистра: phys = mmio + (SEG << 2) + (reg << 2),
// где SEG = DCN_BASE__INST0_SEG<base_idx> (обычно SEG2 = 0x34C0).
#include "amdgpu_dcn.h"
#include "dcn10_regs.h"
#include "amdgpu.h"
#include "io.h"
#include "serial.h"

#define DCN_SEG 0x000034C0u

static unsigned int g_mmio = 0;
static unsigned char g_r = 0x20, g_g = 0x40, g_b = 0xC0;

void amdgpu_caps_led(int on);

static inline volatile unsigned int* reg_ptr(unsigned int reg) {
    return (volatile unsigned int*)(g_mmio + (DCN_SEG << 2) + (reg << 2));
}
static inline unsigned int reg_rd(unsigned int reg) { return *reg_ptr(reg); }
static inline void reg_wr(unsigned int reg, unsigned int val) { *reg_ptr(reg) = val; }
static void reg_upd(unsigned int reg, unsigned int mask, unsigned int val) {
    unsigned int v = reg_rd(reg);
    unsigned int shift = 0, m = mask;
    while (m && !(m & 1)) { m >>= 1; shift++; }
    v = (v & ~mask) | ((val << shift) & mask);
    reg_wr(reg, v);
}

static void stage_mark(int n) {
    serial_print("[dcn] stage "); serial_print_int(n); serial_print("\n");
    for (int i = 0; i < n; i++) {
        amdgpu_caps_led(1);
        for (volatile int d = 0; d < 8000000; d++) { }
        amdgpu_caps_led(0);
        for (volatile int d = 0; d < 8000000; d++) { }
    }
}

// ============================================================
// DCCG: клоки (display clock). BIOS обычно уже включил — минимально.
// ============================================================
static int dcn_init_dccg(void) {
    stage_mark(1);
    // Клоки DCN BIOS уже поднял (иначе не было бы графики). Не трогаем.
    // (Если понадобится — DCCG_DISP_CNTL_REG 0x7f, DCCG_GATE_DISABLE_CNTL 0x74.)
    return 0;
}

// ============================================================
// OPTC (timing generator) — тайминги 640x480@60 (VESA DMT).
// По optc1_program_timing (dcn10_optc.c).
// ============================================================
static int dcn_init_optc(void) {
    stage_mark(2);

    // Тайминги 640x480@60:
    //   H: addressable=640, front_porch=16, sync_width=96, total=800
    //   V: addressable=480, front_porch=10, sync_width=2,  total=525
    unsigned int h_total = 800, h_sync = 96, h_blank_start = 800 - 16;
    unsigned int v_total = 525, v_sync = 2,  v_blank_start = 525 - 10;
    unsigned int h_blank_end = h_blank_start - 640;   // active
    unsigned int v_blank_end = v_blank_start - 480;

    // 1. Выключить VTG (перед программированием).
    reg_upd(mmOTG0_OTG_CONTROL, 0x00000001u, 0);   // OTG_MASTER_EN = 0

    // 2. H_TOTAL = total - 1.
    reg_upd(mmOTG0_OTG_H_TOTAL, 0x00007FFFu, h_total - 1);

    // 3. H_SYNC_A: START=0 (биты 0..14), END=sync_width (биты 16..30).
    reg_wr(mmOTG0_OTG_H_SYNC_A, (0u & 0x7FFF) | ((h_sync & 0x7FFF) << 16));

    // 4. H_BLANK_START_END: START (0..14), END (16..30).
    reg_wr(mmOTG0_OTG_H_BLANK_START_END,
           (h_blank_start & 0x7FFF) | ((h_blank_end & 0x7FFF) << 16));

    // 5. H_SYNC polarity: 1 = negative (VESA 640x480@60 — negative HSync).
    reg_upd(mmOTG0_OTG_H_SYNC_A_CNTL, 0x1u, 1);

    // 6. V_TOTAL = total - 1.
    reg_upd(mmOTG0_OTG_V_TOTAL, 0x00007FFFu, v_total - 1);

    // 7. V_SYNC_A: START=0, END=sync_width.
    reg_wr(mmOTG0_OTG_V_SYNC_A, (0u & 0x7FFF) | ((v_sync & 0x7FFF) << 16));

    // 8. V_BLANK_START_END.
    reg_wr(mmOTG0_OTG_V_BLANK_START_END,
           (v_blank_start & 0x7FFF) | ((v_blank_end & 0x7FFF) << 16));

    // 9. V_SYNC polarity: 1 = negative.
    reg_upd(mmOTG0_OTG_V_SYNC_A_CNTL, 0x1u, 1);

    // 10. OTG_CONTROL: START_POINT_CNTL (0 для не-DP), FIELD_NUMBER_CNTL=0.
    reg_upd(mmOTG0_OTG_CONTROL, 0x3000u, 0);   // START_POINT=0, FIELD=0

    return 0;
}
// Framebuffer: статический буфер в .bss (простой, без pmem).
// Формат 32bpp XRGB8888 (совпадает с удобной записью).
#define DCN_FB_W 640
#define DCN_FB_H 480
static unsigned int g_fb[DCN_FB_W * DCN_FB_H] __attribute__((aligned(4096)));

static int dcn_init_hubp(void) {
    stage_mark(3);

    unsigned int fb_phys = (unsigned int)(unsigned long)g_fb;
    unsigned int pitch = DCN_FB_W * 4;   // 32bpp

    // 0. НАСТРОЙКА VM (identity через SYSTEM_APERTURE) — чтобы HUBP
    //    читал ФИЗИЧЕСКИЕ адреса напрямую, без page table.
    //    Диапазон 0..0xFFFFFFFF (вся память — identity).
    reg_wr(mmHUBPREQ0_DCN_VM_SYSTEM_APERTURE_LOW_ADDR_LSB, 0x0u);
    reg_wr(mmHUBPREQ0_DCN_VM_SYSTEM_APERTURE_LOW_ADDR_MSB, 0x0u);
    reg_wr(mmHUBPREQ0_DCN_VM_SYSTEM_APERTURE_HIGH_ADDR_LSB, 0xFFFFFFFFu);
    reg_wr(mmHUBPREQ0_DCN_VM_SYSTEM_APERTURE_HIGH_ADDR_MSB, 0xFu);
    // default addr = 0 (если промах — читаем с 0).
    reg_wr(mmHUBPREQ0_DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, 0x0u);
    reg_wr(mmHUBPREQ0_DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, 0x0u);
    // TLB: включить L1, SYSTEM_ACCESS_MODE=3 (системная память, identity).
    reg_upd(mmHUBPREQ0_DCN_VM_MX_L1_TLB_CNTL, 0x0000000Fu, 0x3u);  // уточнить маску

    // 1. Адрес поверхности (lo/hi).
    reg_wr(mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS, fb_phys);
    reg_wr(mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0);

    // 2. Pitch (в пикселях для DCSURF_SURFACE_PITCH: поле PITCH = pitch/8? —
    //    в DCN pitch задаётся в блоках; для linear 32bpp = ширина).
    // Ставим ширину.
    reg_wr(mmHUBPREQ0_DCSURF_SURFACE_PITCH, DCN_FB_W);

    // 3. Формат поверхности: SURFACE_CONFIG — pixel format + tiling.
    //    Поле SURFACE_PIXEL_FORMAT = 0 (ARGB8888)? Точные биты — из sh_mask.
    //    Пока — 32bpp, linear.
    reg_wr(mmHUBP0_DCSURF_SURFACE_CONFIG, 0x00000000u);   // уточним по sh_mask

    // 4. Tiling: linear (0).
    reg_wr(mmHUBP0_DCSURF_TILING_CONFIG, 0);

    // 5. Включить HUBP (DCHUBP_CNTL).
    //    HUBP_DISABLE = 0, HUBP_BLANK_EN = 1.
    reg_upd(mmHUBP0_DCHUBP_CNTL, 0x00000011u, 0x00000001u);  // BLANK_EN=1, DISABLE=0

    // Залить буфер цветом.
    unsigned int color = ((unsigned int)g_r << 16) | ((unsigned int)g_g << 8) | g_b;
    for (unsigned int i = 0; i < DCN_FB_W * DCN_FB_H; i++) g_fb[i] = color;

    serial_print("[dcn] HUBP fb=0x"); serial_print_hex(fb_phys); serial_print("\n");
    return 0;
}
// ============================================================
// PIPE: DPP (CNVC_CFG0) — формат пикселей. OPP/MPC — bypass.
// ============================================================
static int dcn_init_pipe(void) {
    stage_mark(4);
    // CNVC_CFG0_FORMAT_CONTROL: формат пикселей входного буфера.
    //   0 = ARGB8888 / 32bpp (уточним по sh_mask; обычно 0 = RGB888).
    reg_wr(mmCNVC_CFG0_FORMAT_CONTROL, 0u);
    // CNVC_CFG0_SURFACE_PIXEL_FORMAT: тоже формат.
    reg_wr(mmCNVC_CFG0_CNVC_SURFACE_PIXEL_FORMAT, 0u);   // 32bpp RGB
    // (Opp/MPC bypass — оставляем как BIOS/по умолчанию.)
    return 0;
}

// ============================================================
// Включить вывод: OTG_CONTROL.MASTER_EN = 1 (запустить timing).
// ============================================================
static int dcn_enable_output(void) {
    stage_mark(5);
    // Запустить OTG (timing generator) pipe 0.
    reg_upd(mmOTG0_OTG_CONTROL, 0x00000001u, 1u);   // OTG_MASTER_EN = 1
    return 0;
}

void amdgpu_dcn_set_color(unsigned char r, unsigned char g, unsigned char b) {
    g_r = r; g_g = g; g_b = b;
}

int amdgpu_dcn_init(unsigned int mmio) {
    if (!mmio) return -1;
    g_mmio = mmio;
    serial_print("[dcn] init, mmio=0x"); serial_print_hex(mmio); serial_print("\n");
    unsigned int v = reg_rd(mmOTG0_OTG_H_TOTAL);
    serial_print("[dcn] OTG0_H_TOTAL = 0x"); serial_print_hex(v); serial_print("\n");
    int rc;
    if ((rc = dcn_init_dccg()))     return rc;
    if ((rc = dcn_init_optc()))     return rc;
    if ((rc = dcn_init_hubp()))     return rc;
    if ((rc = dcn_init_pipe()))     return rc;
    if ((rc = dcn_enable_output())) return rc;
    stage_mark(6);
    serial_print("[dcn] init OK\n");
    return 0;
}
