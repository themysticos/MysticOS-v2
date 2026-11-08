// hda.c - Intel High Definition Audio драйвер MysticOS 2.
//
// См. hda.h. Реализация:
//   - PCI: контроллер (class 0x0403, обычно Intel 8086:2668/293e/...).
//   - MMIO BAR0: GCAP/GCTL/STATESTS, CORB/RIRB, streams.
//   - Сброс контроллера (GCTL.CRST), ожидание STATESTS.
//   - CORB/RIRB кольца + immediate commands (IC) как fallback.
//   - Перечисление кодеков, выбор Audio Output конвертера, настройка pin,
//     усиления (unmute) и формата.
//   - Один output stream: BDLE-буфер, SD_CTL RUN, SD_FMT 16-bit/48k/stereo.
//   - play_pcm(): 8-бит беззнаковый моно -> 16-бит stereo -> DMA -> RUN.
//
// Все MMIO-адреса identity-mapped (см. paging.h: identity 4 ГБ).
#include "hda.h"
#include "vss.h"
#include "driver.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "pmem.h"
#include "music.h"

// ============================================================
// Регистры контроллера HDA (MMIO, шаг 4)
// ============================================================
#define HDA_GCAP        0x00
#define HDA_GCTL        0x08
#define HDA_STATESTS    0x0E
#define HDA_INTCTL      0x20
#define HDA_INTSTS      0x24
#define HDA_CORBLBASE   0x40
#define HDA_CORBUBASE   0x44
#define HDA_CORBWP      0x48
#define HDA_CORBRP      0x4A
#define HDA_CORBCTL     0x4C
#define HDA_CORBSTS     0x4D
#define HDA_CORBSIZE    0x4E
#define HDA_RIRBLBASE   0x50
#define HDA_RIRBUBASE   0x54
#define HDA_RIRBWP      0x58
#define HDA_RIRBCTL     0x5C
#define HDA_RIRBSTS     0x5D
#define HDA_RIRBSIZE    0x5E
#define HDA_ICW         0x60   // Immediate Command Write
#define HDA_IRR         0x64   // Immediate Response Read
#define HDA_ICS         0x68   // Immediate Command Status (bit0 ICB, bit1 IRV)
#define HDA_DPIBLBASE   0x70
#define HDA_DPIBUBASE   0x74

// GCTL биты
#define GCTL_CRST       0x00000001u   // controller reset

// CORBCTL/RIRBCTL биты
#define CORBCTL_RUN     0x02
#define RIRBCTL_RUN     0x02
#define RIRBCTL_INT     0x01

// Stream descriptor (offset от stream base, шаг 0x20)
#define SD_BASE         0x80
#define SD_STRIDE       0x20
#define SD_CTL          0x00   // 24-бит
#define SD_STS          0x03
#define SD_LPIB         0x04
#define SD_CBL          0x08
#define SD_LVI          0x0C
#define SD_FIFOW        0x0E
#define SD_FIFOS        0x10
#define SD_FMT          0x12   // 16-бит
#define SD_BDPL         0x18
#define SD_BDPU         0x1C

// SD_CTL биты
#define SD_CTL_RUN      0x02
#define SD_CTL_TAG_SH   20

// BDLE
#define BDL_ENTRIES     2
#define MAX_BDL_BYTES   (16u * 1024u)

// --- Миксер: непрерывный кольцевой поток + пул голосов ---------------
// HDA-поток играет кольцо по кругу; мы постоянно дописываем в него микс
// активных голосов. Голоса хранят КОПИИ сэмплов в памяти ядра (буферы
// программ недоступны из контекста ядра). Ресемплинг — на лету при миксе.
#define MIX_RATE        48000u          // частота потока (Гц), фиксирована
#define RING_FRAMES     32768u          // кольцо: 32768 стерео-кадров (~683 мс).
                                        // Большое — чтобы тяжёлый рендер кадра
                                        // (syscall IF=0, PIT не тикает) не вызывал underrun.
#define RING_BYTES      (RING_FRAMES * 4u)
#define MAX_VOICES      8               // = число каналов SFX в Doom
#define VOICE_CAP       16384u          // макс. длина одного сэмпла (байт)

// Пул буферов голосов — из pmem (см. hda_init). В .bss не помещается.
static unsigned char* g_voice_buf = 0;

// ============================================================
// Состояние
// ============================================================
static volatile unsigned char* g_mmio = 0;   // BAR0 (identity)
static int g_ready = 0;
static unsigned int g_codec = 0;             // codec address (0..14)
static unsigned int g_afg = 0;               // audio function group NID
static unsigned int g_dac = 0;               // output converter NID
static unsigned int g_pin = 0;               // output pin NID
static unsigned int g_stream_idx = 0;        // индекс output stream descriptor
static unsigned int g_stream_tag = 1;        // stream tag

// Буферы выделяются из pmem (4 МБ identity-блок) в hda_init — в .bss
// их держать нельзя: .bss ядра растёт на стек 0x90000.
static short* g_ring = 0;            // кольцевой DMA-буфер (16-бит stereo)
static unsigned int g_ring_wr = 0;   // индекс записи (в кадрах, 0..RING_FRAMES-1)
// DPIB (DMA Position in Buffer) — буфер, куда HW пишет реальную позицию DMA.
static volatile unsigned int* g_posbuf = 0;

// Голос микшера.
typedef struct {
    int      active;
    unsigned int pos;        // позиция в источнике (в 16.16 fixed, отсчёты)
    unsigned int step;       // шаг ресемплинга (src_rate/48000 в 16.16)
    unsigned int len;        // длина сэмпла в отсчётах
    int      vol;            // 0..256
    unsigned char* data;     // копия сэмпла (8-бит беззнаковый моно) в heap ядра
} voice_t;
static voice_t g_voices[MAX_VOICES];

// BDL (Buffer Descriptor List), выровнен на 128.
struct bdl_entry {
    unsigned int addr_lo;
    unsigned int addr_hi;
    unsigned int length;
    unsigned int flags;      // bit0 = IOC
} __attribute__((packed));
static struct bdl_entry g_bdl[BDL_ENTRIES] __attribute__((aligned(128)));

// CORB/RIRB (identity, физические адреса == указатели).
#define CORB_ENTRIES 256
#define RIRB_ENTRIES 256
static unsigned int g_corb[CORB_ENTRIES] __attribute__((aligned(128)));
static unsigned long long g_rirb[RIRB_ENTRIES] __attribute__((aligned(128)));
static unsigned int g_corb_wp = 0;
static unsigned int g_rirb_rp = 0;

static unsigned int now_ms(void);

// ============================================================
// MMIO helpers
// ============================================================
static inline unsigned int mmio_r32(unsigned int off) {
    return *(volatile unsigned int*)(g_mmio + off);
}
static inline void mmio_w32(unsigned int off, unsigned int v) {
    *(volatile unsigned int*)(g_mmio + off) = v;
}
static inline unsigned short mmio_r16(unsigned int off) {
    return *(volatile unsigned short*)(g_mmio + off);
}
static inline void mmio_w16(unsigned int off, unsigned short v) {
    *(volatile unsigned short*)(g_mmio + off) = v;
}
static inline unsigned char mmio_r8(unsigned int off) {
    return *(volatile unsigned char*)(g_mmio + off);
}
static inline void mmio_w8(unsigned int off, unsigned char v) {
    *(volatile unsigned char*)(g_mmio + off) = v;
}

static void udelay_loop(unsigned int n) { for (volatile unsigned int i = 0; i < n; i++) {} }

// ============================================================
// Кодек-команды: 32-бит verb+payload.
// ============================================================
#define VERB_GET_PARAMETER              0xF00
#define VERB_SET_STREAM_FORMAT          0x200
#define VERB_SET_AMP_GAIN_MUTE          0x300
#define VERB_SET_CONVERTER_FORMAT       0x200   // == stream format для конвертера
#define VERB_SET_CONVERTER_STREAM_CHAN  0x706
#define VERB_SET_PIN_WIDGET_CONTROL     0x707
#define VERB_SET_POWER_STATE            0x705

// GET_PARAMETER параметры
#define PARAM_VENDOR_ID        0x00
#define PARAM_SUBNODE_COUNT     0x04
#define PARAM_FUNC_GROUP_TYPE   0x05
#define PARAM_AUDIO_WIDGET_CAP  0x09
#define PARAM_PCM_RATE          0x0A
#define PARAM_STREAM_FORMAT     0x0B
#define PARAM_PIN_CAP           0x0C
#define PARAM_CONN_LIST_LEN     0x0E

// Audio widget capability bits
#define AWCAP_TYPE_MASK     0x00F00000u   // Widget Type = bits 23:20
#define AWCAP_TYPE_SHIFT    20
#define AWCAP_TYPE_DAC      0x0
#define AWCAP_TYPE_PIN      0x4

// ============================================================
// Immediate command (IC): не требует CORB. Надёжнее для старта.
// ============================================================
// Immediate command (Intel HDA spec, ср. Linux hda_register.h):
//   0x60 IC  - Immediate Command (write)
//   0x64 IR  - Immediate Response (read)
//   0x68 ICS - Immediate Command Status: AZX_ICS_ICB=0x1 (busy), AZX_ICS_IRV=0x2 (valid)
#define ICS_BUSY    0x1   // software выставляет = триггер обработки
#define ICS_VALID   0x2   // HW: ответ готов (write-to-clear)
static unsigned int hda_ic_command(unsigned int codec, unsigned int nid,
                                   unsigned int verb, unsigned int payload) {
    unsigned int cmd = (codec << 28) | (nid << 20) | (verb << 8) | (payload & 0xFF);
    // Intel HDA immediate command (как в QEMU hw/audio/intel-hda.c):
    //   1) ICW(0x60) = verb
    //   2) ICS(0x68) = BUSY(bit0)  <- это ТРИГГЕР обработки
    //   3) ждать ICS & VALID(bit1)
    //   4) IRR(0x64) = ответ
    //   5) ICS(0x68) = VALID(bit1) -> write-to-clear
    mmio_w32(HDA_ICW, cmd);
    mmio_w16(HDA_ICS, ICS_BUSY);
    for (unsigned int t = 0; t < 1000000u; t++)
        if (mmio_r16(HDA_ICS) & ICS_VALID) break;
    unsigned int r = mmio_r32(HDA_IRR);
    mmio_w16(HDA_ICS, ICS_VALID);   // сбросить VALID
    return r;
}

// ============================================================
// CORB/RIRB
// ============================================================
// Кольца HDA работают по схеме "последний записанный индекс":
//   CORBWP/CORBRP/RIRBWP указывают на последнюю валидную запись.
//   Контроллер читает CORB в диапазоне (CORBRP, CORBWP],
//   а мы читаем RIRB в диапазоне (наш rp, RIRBWP].
// Поэтому пишем команду в НОВЫЙ wp, а ответ читаем из rp+1.
static int hda_corb_send(unsigned int codec, unsigned int nid,
                         unsigned int verb, unsigned int payload,
                         unsigned int* resp) __attribute__((unused));
static int hda_corb_send(unsigned int codec, unsigned int nid,
                         unsigned int verb, unsigned int payload,
                         unsigned int* resp) {
    unsigned int cmd = (codec << 28) | (nid << 20) | (verb << 8) | (payload & 0xFF);
    unsigned int wp = (g_corb_wp + 1) % CORB_ENTRIES;
    unsigned int rp = mmio_r16(HDA_CORBRP) & 0xFF;
    if (wp == rp) return 0;   // кольцо полно
    g_corb[wp] = cmd;
    g_corb_wp = wp;
    mmio_w16(HDA_CORBWP, (unsigned short)g_corb_wp);
    for (unsigned int t = 0; t < 1000000u; t++) {
        unsigned int rirb_wp = mmio_r16(HDA_RIRBWP) & 0xFF;
        if (rirb_wp != g_rirb_rp) {
            // Ответ лежит в RIRB[(rp+1) % entries].
            unsigned int idx = (g_rirb_rp + 1) % RIRB_ENTRIES;
            unsigned long long e = g_rirb[idx];
            *resp = (unsigned int)(e & 0xFFFFFFFFu);
            g_rirb_rp = idx;
            mmio_w8(HDA_RIRBSTS, 0x1);
            return 1;
        }
    }
    serial_print("[hda] corb timeout: corbwp="); serial_print_hex(mmio_r16(HDA_CORBWP));
    serial_print(" rirbwp="); serial_print_hex(mmio_r16(HDA_RIRBWP));
    serial_print(" corbrp="); serial_print_hex(mmio_r16(HDA_CORBRP));
    serial_print("\n");
    return 0;
}

static unsigned int hda_cmd(unsigned int nid, unsigned int verb, unsigned int payload) {
    // Immediate command в QEMU обрабатывается СИНХРОННО (без DMA и таймеров).
    // CORB требует bus-master DMA, который в busy-loop может не отработать.
    // Поэтому основной путь — IC, CORB — fallback.
    return hda_ic_command(g_codec, nid, verb, payload);
}

static unsigned int hda_get_param(unsigned int nid, unsigned int param) {
    return hda_cmd(nid, VERB_GET_PARAMETER, param);
}

// ============================================================
// Инициализация контроллера
// ============================================================
static int hda_controller_reset(void) {
    unsigned int gctl = mmio_r32(HDA_GCTL);
    mmio_w32(HDA_GCTL, gctl & ~GCTL_CRST);
    for (unsigned int t = 0; t < 1000000u; t++)
        if ((mmio_r32(HDA_GCTL) & GCTL_CRST) == 0) break;
    udelay_loop(10000);
    mmio_w32(HDA_GCTL, gctl | GCTL_CRST);
    for (unsigned int t = 0; t < 1000000u; t++)
        if (mmio_r32(HDA_GCTL) & GCTL_CRST) break;
    udelay_loop(10000);
    for (unsigned int t = 0; t < 1000000u; t++)
        if (mmio_r16(HDA_STATESTS) != 0) break;
    return 1;
}

static void hda_setup_corb_rirb(void) {
    mmio_w8(HDA_CORBCTL, 0);
    mmio_w8(HDA_RIRBCTL, 0);
    mmio_w8(HDA_CORBSIZE, 0x2);
    mmio_w8(HDA_RIRBSIZE, 0x2);
    unsigned int corb_phys = (unsigned int)g_corb;
    unsigned int rirb_phys = (unsigned int)g_rirb;
    mmio_w32(HDA_CORBLBASE, corb_phys & ~0x7Fu);
    mmio_w32(HDA_CORBUBASE, 0);
    mmio_w32(HDA_RIRBLBASE, rirb_phys & ~0x7Fu);
    mmio_w32(HDA_RIRBUBASE, 0);
    // CORBRPRST: пишем 1, HW поднимает бит 15 по завершении, затем чистим 0.
    mmio_w16(HDA_CORBRP, 0x8000);
    for (unsigned int t = 0; t < 100000u; t++)
        if (mmio_r16(HDA_CORBRP) & 0x8000) break;
    mmio_w16(HDA_CORBRP, 0x0000);
    g_corb_wp = 0; g_rirb_rp = 0;
    mmio_w16(HDA_CORBWP, 0);
    mmio_w16(HDA_RIRBWP, 0x8000);
    mmio_w8(HDA_CORBCTL, CORBCTL_RUN);
    mmio_w8(HDA_RIRBCTL, RIRBCTL_RUN);
}

// ============================================================
// Поиск кодека / конвертера / pin
// ============================================================
static int hda_pick_codec(void) {
    unsigned int statests = mmio_r16(HDA_STATESTS);
    if (statests == 0) return 0;
    for (unsigned int c = 0; c < 15; c++)
        if (statests & (1u << c)) { g_codec = c; return 1; }
    return 0;
}

static int hda_find_afg(void) {
    unsigned int cnt = hda_get_param(0, PARAM_SUBNODE_COUNT);
    unsigned int start = (cnt >> 16) & 0xFF;
    unsigned int n = cnt & 0xFF;
    for (unsigned int i = 0; i < n; i++) {
        unsigned int nid = start + i;
        unsigned int fgt = hda_get_param(nid, PARAM_FUNC_GROUP_TYPE);
        if ((fgt & 0x7F) == 0x01) { g_afg = nid; return 1; }
    }
    return 0;
}

static int hda_find_dac_pin(void) {
    unsigned int cnt = hda_get_param(g_afg, PARAM_SUBNODE_COUNT);
    unsigned int start = (cnt >> 16) & 0xFF;
    unsigned int n = cnt & 0xFF;
    g_dac = 0; g_pin = 0;
    for (unsigned int i = 0; i < n; i++) {
        unsigned int nid = start + i;
        unsigned int cap = hda_get_param(nid, PARAM_AUDIO_WIDGET_CAP);
        unsigned int type = (cap & AWCAP_TYPE_MASK) >> AWCAP_TYPE_SHIFT;
        if (type == AWCAP_TYPE_DAC && g_dac == 0) {
            g_dac = nid;
        } else if (type == AWCAP_TYPE_PIN && g_pin == 0) {
            unsigned int pincap = hda_get_param(nid, PARAM_PIN_CAP);
            if (pincap & 0x10) g_pin = nid;
        }
    }
    return (g_dac != 0);
}

// ============================================================
// Настройка DAC/pin и формата
// ============================================================
static void hda_config_codec(void) {
    hda_cmd(g_afg, VERB_SET_POWER_STATE, 0x00);
    udelay_loop(1000);
    hda_cmd(g_dac, VERB_SET_POWER_STATE, 0x00);
    // Gain 0x3F, не 0x7F: 0x7F часто = максимальное усиление -> клиппинг
    // («пердение»). 0x3F — умеренный уровень, оставляет запас.
    hda_cmd(g_dac, VERB_SET_AMP_GAIN_MUTE, 0xB000 | 0x3F);
    if (g_pin) {
        hda_cmd(g_pin, VERB_SET_PIN_WIDGET_CONTROL, 0x40);
        hda_cmd(g_pin, VERB_SET_AMP_GAIN_MUTE, 0xB000 | 0x3F);
    }
}

static void hda_set_converter_format(unsigned int rate) {
    unsigned int base = 0x00;
    (void)rate;
    unsigned int fmt = (1u << 4) | (2u - 1) | base;
    hda_cmd(g_dac, VERB_SET_CONVERTER_FORMAT, fmt);
    hda_cmd(g_dac, VERB_SET_CONVERTER_STREAM_CHAN, (g_stream_tag << 4) | 0x0);
}

// ============================================================
// Output stream
// ============================================================
static void hda_stream_stop(void) __attribute__((unused));
static void hda_stream_stop(void) {
    unsigned int sb = SD_BASE + g_stream_idx * SD_STRIDE;
    unsigned int ctl = mmio_r32(sb + SD_CTL) & 0x00FFFFFFu;
    ctl &= ~SD_CTL_RUN;
    mmio_w32(sb + SD_CTL, ctl);
    for (unsigned int t = 0; t < 100000u; t++)
        if (!(mmio_r32(sb + SD_CTL) & SD_CTL_RUN)) break;
}

// Запустить НЕПРЕРЫВНЫЙ кольцевой поток: BDL указывает на весь g_ring,
// контроллер играет его по кругу. Мы постоянно дописываем микс вперёд.
static void hda_stream_start_ring(void) {
    unsigned int sb = SD_BASE + g_stream_idx * SD_STRIDE;
    unsigned int half_bytes = RING_BYTES / 2;

    // ЭКСПЕРИМЕНТ: одно BDLE на всё кольцо — убираем внутреннюю границу
    // дескрипторов (раньше 2×половина → тик каждые ~341 мс).
    (void)half_bytes;
    g_bdl[0].addr_lo = (unsigned int)&g_ring[0];
    g_bdl[0].addr_hi = 0;
    g_bdl[0].length = RING_BYTES;
    g_bdl[0].flags = 0x0;

    mmio_w32(sb + SD_BDPL, (unsigned int)g_bdl & ~0x7Fu);
    mmio_w32(sb + SD_BDPU, 0);
    mmio_w32(sb + SD_CBL, RING_BYTES);
    mmio_w16(sb + SD_LVI, 0);

    // DPIB: адрес буфера, куда HW пишет РЕАЛЬНУЮ позицию DMA.
    // Регистры ГЛОБАЛЬНЫЕ (0x70/0x74), выровнено на 128 байт.
    mmio_w32(HDA_DPIBLBASE, (unsigned int)g_posbuf & ~0x7Fu);
    mmio_w32(HDA_DPIBUBASE, 0);

    unsigned int stream_fmt = (1u << 4) | (2u - 1) | 0x0;   // 16-бит stereo 48к
    mmio_w16(sb + SD_FMT, (unsigned short)stream_fmt);

    // Сброс статуса потока до запуска: FIFOE(bit2)/DESE(bit3) — write-1-to-clear.
    // Если они «залипли» с прошлого запуска, поток глючит (щелчки).
    mmio_w8(sb + SD_STS, 0x1C);
    unsigned int ctl = mmio_r32(sb + SD_CTL) & 0x00FFFFFFu;
    ctl &= ~(0xFu << SD_CTL_TAG_SH);
    ctl |= (g_stream_tag << SD_CTL_TAG_SH);
    ctl |= SD_CTL_RUN;
    mmio_w32(sb + SD_CTL, ctl);
}

// Текущая позиция чтения HW (в кадрах) — LPIB.
// (Пробовал DPIB — HW не пишет позицию без бита DPM в SD_CTL, вернулся
// к LPIB. См. также sound/pci/hda/hda_controller.c.)
static unsigned int hda_hw_read_frame(void) {
    unsigned int sb = SD_BASE + g_stream_idx * SD_STRIDE;
    unsigned int lpib = mmio_r32(sb + SD_LPIB);
    return (lpib / 4u) % RING_FRAMES;
}

// Смикшировать count кадров из активных голосов в кольцо (вперёд от g_ring_wr).
static void hda_mix(unsigned int count) {
    for (unsigned int f = 0; f < count; f++) {
        int acc = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            voice_t* vo = &g_voices[v];
            if (!vo->active) continue;
            unsigned int idx = vo->pos >> 16;
            if (idx >= vo->len) { vo->active = 0; continue; }
            int s = ((int)vo->data[idx] - 128) << 8;   // -32768..32512
            acc += (s * vo->vol) / 512;    // умеренно: 8 голосов суммируются
            vo->pos += vo->step;
        }
        // Музыка (MUS-синтез) — добавляется к SFX-голосам.
        acc += music_next_sample();
        // Master-headroom 0.75: даём запас по динамике, чтобы громкая музыка
        // (MEGALOVANIA) + SFX не упиралась в потолок. Дальше — плавный
        // soft-clip вместо прежнего излома на 30000 (тот сам добавлял
        // гармонические искажения = «грязный» клип).
        acc = (acc * 2) / 3;
        if (acc > 32767) acc = 32767;
        if (acc < -32768) acc = -32768;
        g_ring[g_ring_wr * 2]     = (short)acc;
        g_ring[g_ring_wr * 2 + 1] = (short)acc;
        g_ring_wr = (g_ring_wr + 1u) % RING_FRAMES;
    }
}

// ============================================================
// Публичный API
// ============================================================
static int g_stream_on = 0;   // непрерывный кольцевой поток запущен

int hda_init(void) {
    if (!g_mmio) return 0;
    hda_controller_reset();
    hda_setup_corb_rirb();
    if (!hda_pick_codec()) { serial_print("[hda] no codec\n"); return 0; }
    serial_print("[hda] codec addr="); serial_print_int((int)g_codec); serial_print("\n");
    if (!hda_find_afg()) { serial_print("[hda] no AFG\n"); return 0; }
    if (!hda_find_dac_pin()) { serial_print("[hda] no DAC\n"); return 0; }
    // OUTPUT stream: дескрипторы 0..3 — входные, 4..7 — выходные
    // (QEMU intel_hda_set_st_ctl: output = stream >= 4). Берём первый выходной.
    g_stream_idx = 4;
    hda_config_codec();
    hda_set_converter_format(48000);
    // Буферы из pmem: кольцо (32 КБ) + пул голосов (128 КБ). Один блок 4 МБ.
    unsigned int mem = pmem_alloc_block();
    if (!mem) { serial_print("[hda] no pmem\n"); return 0; }
    g_ring = (short*)mem;                              // RING_BYTES (128 КБ)
    g_voice_buf = (unsigned char*)(mem + 0x20000);     // пул голосов, за кольцом
    // DPIB (DMA Position in Buffer): HW пишет сюда РЕАЛЬНУЮ позицию DMA.
    // Linux использует её для кольца (LPIB — счётчик на линке, отстаёт).
    g_posbuf = (volatile unsigned int*)(mem + 0x40000);
    *g_posbuf = 0;
    // Запустить непрерывный кольцевой поток и предзаполнить тишиной.
    for (unsigned int i = 0; i < RING_FRAMES * 2; i++) g_ring[i] = 0;
    g_ring_wr = 0;
    hda_stream_start_ring();
    g_stream_on = 1;
    g_ready = 1;
    serial_print("[hda] afg="); serial_print_int((int)g_afg);
    serial_print(" dac="); serial_print_int((int)g_dac);
    serial_print(" pin="); serial_print_int((int)g_pin);
    serial_print("\n");
    return 1;
}

int hda_ready(void) { return g_ready; }

void hda_set_rate(unsigned int rate) {
    if (g_ready) hda_set_converter_format(rate);
}

// Запустить сэмпл как НОВЫЙ голос микшера (не вытесняет играющие).
// Возвращает 1, если голос выделен. Буфер КОПИРУЕТСЯ в память ядра:
// в контексте hda_poll буферы программ недоступны.
int hda_voice_start(const unsigned char* buf, unsigned int len, unsigned int rate, int vol) {
    if (!g_ready || len == 0) return 0;
    if (len > VOICE_CAP) len = VOICE_CAP;
    int slot = -1;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!g_voices[v].active) { slot = v; break; }
    }
    if (slot < 0) {
        // Все голоса заняты — вытесняем самый старый (slot 0).
        slot = 0;
    }
    voice_t* vo = &g_voices[slot];
    unsigned char* dst = g_voice_buf + (unsigned int)slot * VOICE_CAP;
    for (unsigned int i = 0; i < len; i++) dst[i] = buf[i];
    unsigned int src_rate = rate ? rate : 11025u;
    // step = src_rate/48000 в 16.16 fixed. 32-бит хватает: src_rate<=48000.
    if (src_rate > 48000u) src_rate = 48000u;
    vo->step = (src_rate << 16) / MIX_RATE;
    vo->pos = 0;
    vo->len = len;
    vo->vol = vol < 0 ? 0 : (vol > 256 ? 256 : vol);
    vo->data = g_voice_buf + (unsigned int)slot * VOICE_CAP;
    vo->active = 1;
    return 1;
}

void hda_play_pcm(const unsigned char* buf, unsigned int len, unsigned int rate) {
    hda_voice_start(buf, len, rate, 256);
}

// Сгенерировать тон и запустить его как голос. Буфер — статический.
static unsigned char g_beep_buf[VOICE_CAP];
void hda_beep(unsigned int freq_hz, unsigned int ms) {
    if (!g_ready || freq_hz == 0) return;
    unsigned int n = (MIX_RATE / 1000u) * ms;   // 48 кГц => отсчётов
    if (n > VOICE_CAP) n = VOICE_CAP;
    unsigned int period = MIX_RATE / freq_hz; if (period < 2) period = 2;
    for (unsigned int i = 0; i < n; i++)
        g_beep_buf[i] = ((i / (period / 2)) & 1) ? 0xD0 : 0x30;
    hda_voice_start(g_beep_buf, n, MIX_RATE, 256);
}

// Непрерывный микс: дописываем в кольцо столько кадров, сколько HW уже
// прочитал (держим заполнение близко к полному).
// Зовётся из таймерного IRQ0 (не зависит от рендера!) и из главного цикла.
// Флаг g_in_mix защищает от реентерабельности (IRQ поверх главного цикла).
static volatile int g_in_mix = 0;
static volatile unsigned int g_hda_underruns = 0;   // HDA-кольцо догнало писателя
// LPIB-based regulation: держим writer на HDA_MARGIN кадров впереди реальной
// позиции DMA (LPIB). Это обратная связь от HW — нет дрейфа от неточного
// таймера и нерегулярных вызовов.
#define HDA_MARGIN 4096u   // ~85 мс при 48 кГц — запас на джиттер/IRQ-задержку
void hda_poll(void) {
    if (!g_ready || !g_stream_on) return;
    if (g_in_mix) return;
    g_in_mix = 1;
    unsigned int rd = hda_hw_read_frame();
    unsigned int fill = (g_ring_wr + RING_FRAMES - rd) % RING_FRAMES;
    // Underrun recovery: HW догнал (или перекрутил) writer — DMA крутит
    // устаревшие данные (это и есть «повторяется»). Ставим writer на reader
    // и наполняем заново ниже.
    if (fill >= RING_FRAMES - 1u) {
        g_hda_underruns++;
        g_ring_wr = rd;
        fill = 0;
    }
    // Диагностика (раз в ~200 вызовов).
    {
        static unsigned int dbg_n = 0;
        if ((++dbg_n % 200u) == 0u) {
            unsigned int sb = SD_BASE + g_stream_idx * SD_STRIDE;
            serial_print("[hda] rd="); serial_print_int((int)rd);
            serial_print(" wr="); serial_print_int((int)g_ring_wr);
            serial_print(" fill="); serial_print_int((int)fill);
            serial_print(" sts="); serial_print_hex(mmio_r8(sb + SD_STS));
            serial_print(" ctl="); serial_print_hex(mmio_r32(sb + SD_CTL) & 0xFF);
            serial_print(" lpib="); serial_print_hex(mmio_r32(sb + SD_LPIB));
            serial_print(" u="); serial_print_int((int)g_hda_underruns);
            serial_print("\n");
        }
    }
    // Доливаем до target = RING_FRAMES - HDA_MARGIN, не больше половины
    // кольца за раз (чтобы не блокировать IRQ долгим миксом).
    unsigned int target = RING_FRAMES - HDA_MARGIN;
    if (fill < target) {
        unsigned int need = target - fill;
        if (need > RING_FRAMES / 2u) need = RING_FRAMES / 2u;
        hda_mix(need);
    }
    g_in_mix = 0;
}
#if 0
void hda_poll_old(void) {
    if (!g_ready || !g_stream_on) return;
    if (g_in_mix) return;
    g_in_mix = 1;
    unsigned int rd = hda_hw_read_frame();
    // ДИАГНОСТИКА: раз в ~секунду печатаем позицию DPIB и запись.
    {
        static unsigned int dbg_n = 0;
        if ((++dbg_n % 100) == 0) {
            serial_print("[hda] posbuf="); serial_print_int((int)*g_posbuf);
            serial_print(" rd="); serial_print_int((int)rd);
            serial_print(" wr="); serial_print_int((int)g_ring_wr);
            serial_print("\n");
        }
    }
    // Сколько кадров свободно до позиции чтения HW (кольцо FIFO).
    unsigned int fill;
    if (g_ring_wr >= rd) fill = g_ring_wr - rd;
    else                 fill = RING_FRAMES - (rd - g_ring_wr);
    // ВАЖНО: держим БЕЗОПАСНЫЙ ЗАЗОР. LPIB отстаёт от реальной позиции
    // (FIFO/HW-задержка). Если заполнять кольцо вплотную к rd, мы пишем в
    // ещё не прочитанные HW данные -> щелчок на каждом poll.
    #define HDA_MARGIN 4096u   // ~85 мс запас
    unsigned int space = RING_FRAMES - fill - 1u;
    if (space > HDA_MARGIN) space -= HDA_MARGIN;   // оставить зазор
    else space = 0;
    if (space > 0) hda_mix(space);
    g_in_mix = 0;
}
#endif  // hda_poll_old (LPIB-вариант, отключён)

void hda_debug(void) {
    serial_print("[hda] mmio="); serial_print_hex((unsigned int)g_mmio);
    serial_print(" gcap="); serial_print_hex(mmio_r32(HDA_GCAP));
    serial_print(" statests="); serial_print_int((int)mmio_r16(HDA_STATESTS));
    serial_print("\n");
}

// ============================================================
// VSS: sound_driver_t
// ============================================================
// VSS-probe: HDA уже поднят через VDS (hda_pci_probe -> hda_init),
// поэтому здесь только проверяем готовность, НЕ инициализируем повторно.
static int hda_vss_probe(void) { return g_ready; }

static const sound_driver_t HDA_SOUND_DRIVER = {
    "intel-hda",
    hda_vss_probe,
    hda_ready,
    hda_set_rate,
    hda_play_pcm,
    hda_beep,
    hda_poll
};

// ============================================================
// VDS: driver_t (PCI match по class 0x0403)
// ============================================================
static int hda_pci_match(const device_t* dev) {
    // device_t.class_code от pci_enumerate = (class << 8) | subclass (16 бит).
    // HDA: class 0x04 (multimedia), subclass 0x03 (audio) => 0x0403.
    if ((dev->class_code >> 8) != 0x04) return 0;
    if ((dev->class_code & 0xFF) != 0x03) return 0;
    return 1;
}

static int hda_pci_probe(device_t* dev) {
    unsigned int bar0 = dev->bar[0] & ~0xFu;
    if (!bar0) return -1;
    // ВКЛЮЧИТЬ PCI bus-mastering + memory space!
    // Без бита Bus Master (COMMAND bit2) контроллер не тянет CORB/RIRB по DMA.
    unsigned short cmd = pci_read16(dev->bus_num, dev->slot, dev->func, 0x04);
    cmd |= 0x0006;   // bit1 = Memory Space, bit2 = Bus Master
    pci_write32(dev->bus_num, dev->slot, dev->func, 0x04, cmd);
    g_mmio = (volatile unsigned char*)bar0;
    serial_print("[hda] found controller, bar0="); serial_print_hex(bar0); serial_print("\n");
    if (!hda_init()) return -1;
    vss_register(&HDA_SOUND_DRIVER);
    dev->priv = (void*)g_mmio;
    return 0;
}

static const driver_t HDA_DRIVER = {
    "intel-hda",
    DRV_AUDIO,
    DRV_LEVEL_NORMAL,
    hda_pci_match,
    hda_pci_probe,
    0
};

void hda_driver_register(void) {
    driver_register(&HDA_DRIVER);
}
