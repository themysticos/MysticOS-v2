// virtio_net.c - драйвер virtio-net (минимальный, без offload).
//
// Протокол: каждый пакет в очереди обёрнут в 12-байтный virtio_net_hdr.
// RX: pre-fill RX queue пустыми буферами. QEMU пишет туда пакет + hdr.
// TX: формируем пакет + hdr, кладём в TX queue, notify, wait used.
//
// Features: минимальные — VIRTIO_NET_F_MAC (MAC из device config),
// VIRTIO_F_VERSION_1 (modern). Остальные — не включаем.
#include "virtio_net.h"
#include "virtio.h"
#include "serial.h"

// ============================================================
// Feature bits (часть — стандарт virtio-net, см. Linux virtio_net.h)
// ============================================================
#define VIRTIO_NET_F_CSUM        0    // 1<<0
#define VIRTIO_NET_F_MAC         5    // 1<<5
#define VIRTIO_NET_F_STATUS      16
#define VIRTIO_NET_F_MRG_RXBUF   15
// VIRTIO_F_VERSION_1 (bit 32) — обязательный.

// virtio_net_hdr_mrg_rxbuf (12 байт). Linux: sizeof(struct
// virtio_net_hdr_mrg_rxbuf) = 10 (virtio_net_hdr) + 2 (num_buffers).
// ВАЖНО: для modern (VIRTIO_F_VERSION_1 negotiated) QEMU ТРЕБУЕТ
// 12-байтный hdr даже без MRG_RXBUF (см. virtio_net.c:6928-6930).
// Раньше тут было 10 байт — QEMU получал ARP со сдвигом на 2 -> игнор.
typedef struct {
    v_u8  flags;
    v_u8  gso_type;
    v_u16 hdr_len;
    v_u16 gso_size;
    v_u16 csum_start;
    v_u16 csum_offset;
    v_u16 num_buffers;   // 12-й байт, для modern всегда 0
} __attribute__((packed)) virtio_net_hdr_t;

// ============================================================
// Состояние драйвера
// ============================================================
#define VQ_SIZE 16              // 16 дескрипторов в каждой очереди
#define RX_BUF_SZ 2048          // MTU 1500 + virtio_net_hdr + запас

static virtio_dev_t g_dev;
static int g_ready = 0;
static unsigned char g_mac[6] = {0,0,0,0,0,0};

// Очереди
#define VQ_RX 0
#define VQ_TX 1

// Буферы очередей — по байтам, с выравниванием.
static unsigned char rx_desc_buf[VQ_SIZE * 16] __attribute__((aligned(16)));
static unsigned char rx_avail_buf[4 + VQ_SIZE * 2] __attribute__((aligned(2)));
static unsigned char rx_used_buf[4 + VQ_SIZE * 8] __attribute__((aligned(4)));
static unsigned char rx_pkt_buf[VQ_SIZE * RX_BUF_SZ] __attribute__((aligned(16)));

static unsigned char tx_desc_buf[VQ_SIZE * 16] __attribute__((aligned(16)));
static unsigned char tx_avail_buf[4 + VQ_SIZE * 2] __attribute__((aligned(2)));
static unsigned char tx_used_buf[4 + VQ_SIZE * 8] __attribute__((aligned(4)));
static unsigned char tx_pkt_buf[RX_BUF_SZ] __attribute__((aligned(16)));

// Следующий RX-буфер, который мы отдаём устройству.
static int rx_next_free = 0;

// ============================================================
// match / probe
// ============================================================
int virtio_net_match(const device_t* dev) {
    if (dev->vendor != 0x1AF4) return 0;
    if (dev->device == 0x1000) return 1;   // legacy/transitional
    if (dev->device == 0x1041) return 1;   // modern
    return 0;
}

static void rx_fill_one(void) {
    // Кладём один буфер [hdr][data] в RX queue.
    int i = rx_next_free;
    rx_next_free = (rx_next_free + 1) % VQ_SIZE;
    unsigned char* pkt = rx_pkt_buf + i * RX_BUF_SZ;
    // desc.addr — физ. адрес буфера (identity = virt).
    virtio_queue_add(&g_dev, VQ_RX,
                     (v_u64)(v_u32)pkt,
                     RX_BUF_SZ,
                     VIRTQ_DESC_F_WRITE);   // device пишет в буфер
}

static void rx_fill_all(void) {
    for (int i = 0; i < VQ_SIZE; i++) rx_fill_one();
    virtio_queue_notify(&g_dev, VQ_RX);
}

int virtio_net_probe(device_t* dev) {
    if (!virtio_find_pci(dev->device, &g_dev)) return -1;

    // Features: MAC + VERSION_1.
    v_u64 want = (v_u64)(1ULL << VIRTIO_NET_F_MAC)
               | (v_u64)(1ULL << VIRTIO_NET_F_MRG_RXBUF)   // bit 15
               | (v_u64)(1ULL << 32);   // VIRTIO_F_VERSION_1
    if (!virtio_init(&g_dev, want)) return -2;

    // MAC из device config (первые 6 байт).
    if (g_dev.device_cfg) {
        for (int i = 0; i < 6; i++) g_mac[i] = g_dev.device_cfg[i];
    }
    serial_print("[virtio-net] MAC ");
    for (int i = 0; i < 6; i++) {
        serial_print_hex(g_mac[i]);
        if (i < 5) serial_print(":");
    }
    serial_print("\n");

    // RX queue.
    if (!virtio_queue_setup(&g_dev, VQ_RX, VQ_SIZE,
                            rx_desc_buf, rx_avail_buf, rx_used_buf)) {
        serial_print("[virtio-net] RX queue setup failed\n");
        return -3;
    }
    // TX queue.
    if (!virtio_queue_setup(&g_dev, VQ_TX, VQ_SIZE,
                            tx_desc_buf, tx_avail_buf, tx_used_buf)) {
        serial_print("[virtio-net] TX queue setup failed\n");
        return -4;
    }

    // Pre-fill RX.
    rx_next_free = 0;
    rx_fill_all();

    // DRIVER_OK — ТОЛЬКО теперь, после настройки всех очередей и pre-fill RX.
    virtio_driver_ok(&g_dev);
    serial_print("[virtio-net] DRIVER_OK set\n");

    // ВАЖНО: notify RX ПОСЛЕ DRIVER_OK. Спецификация VirtIO (§3.1.1):
    // device НЕ обрабатывает буферы до DRIVER_OK. Наш первый notify (в
    // rx_fill_all) был проигнорирован QEMU. Переуведомляем — теперь
    // device увидит 16 pre-fill'нутых буферов.
    virtio_queue_notify(&g_dev, VQ_RX);
    serial_print("[virtio-net] re-notified RX after DRIVER_OK\n");

    serial_print("[virtio-net] RX notify_off=");
    serial_print_int((int)g_dev.queues[VQ_RX].queue_notify_off);
    serial_print(" TX notify_off=");
    serial_print_int((int)g_dev.queues[VQ_TX].queue_notify_off);
    serial_print("\n");

    g_ready = 1;
    serial_print("[virtio-net] ready\n");
    return 0;
}

// ============================================================
// API
// ============================================================
int virtio_net_ready(void) { return g_ready; }
const unsigned char* virtio_net_mac(void) { return g_mac; }

int virtio_net_send(const void* data, unsigned int len) {
    if (!g_ready) return -1;
    if (len + sizeof(virtio_net_hdr_t) > RX_BUF_SZ) return -1;

    // Заполняем hdr + данные в tx_pkt_buf.
    virtio_net_hdr_t* h = (virtio_net_hdr_t*)tx_pkt_buf;
    h->flags = 0; h->gso_type = 0; h->hdr_len = 0;
    h->gso_size = 0; h->csum_start = 0; h->csum_offset = 0;
    h->num_buffers = 0;   // modern: всегда 0 (нет mergeable buffers)

    unsigned char* dst = tx_pkt_buf + sizeof(virtio_net_hdr_t);
    const unsigned char* src = (const unsigned char*)data;
    for (unsigned int i = 0; i < len; i++) dst[i] = src[i];

    // Один дескриптор: весь буфер (hdr+data).
    int d = virtio_queue_add(&g_dev, VQ_TX,
                             (v_u64)(v_u32)tx_pkt_buf,
                             len + sizeof(virtio_net_hdr_t),
                             0);   // 0 = device читает
    if (d < 0) return -2;

    serial_print("[virtio-net] send ");
    serial_print_int((int)len);
    serial_print(" bytes, desc=");
    serial_print_int(d);
    serial_print(" avail_idx=");
    serial_print_int((int)g_dev.queues[VQ_TX].avail->idx);
    serial_print("\n");

    virtio_queue_notify(&g_dev, VQ_TX);
    serial_print("[virtio-net] notified TX\n");
    // НЕ ждём used — QEMU обработает асинхронно, но нам не надо ждать.
    // Проверим used при следующем send (или в net_poll). Это не критично:
    // если TX-очередь переполнится, следующий send вернёт ошибку.
    v_u32 used_len = 0;
    (void)virtio_queue_wait(&g_dev, VQ_TX, &used_len);   // «убрать» used, если уже есть
    return (int)len;
}

int virtio_net_recv(void* buf, unsigned int max) {
    if (!g_ready) return -1;
    v_u32 used_len = 0;
    int d = virtio_queue_wait(&g_dev, VQ_RX, &used_len);
    if (d < 0) return -1;

    // d = индекс дескриптора. Вычисляем, какой буфер это был.
    unsigned char* pkt = (unsigned char*)(v_u32)g_dev.queues[VQ_RX].desc[d].addr;
    // Первые 12 байт — virtio_net_hdr. Отдаём данные после него.
    unsigned int data_len = used_len;
    if (data_len < sizeof(virtio_net_hdr_t)) return -1;
    data_len -= sizeof(virtio_net_hdr_t);
    if (data_len > max) data_len = max;
    unsigned char* src = pkt + sizeof(virtio_net_hdr_t);
    unsigned char* dst = (unsigned char*)buf;
    for (unsigned int i = 0; i < data_len; i++) dst[i] = src[i];

    // Вернуть буфер в RX queue (pre-fill).
    rx_fill_one();
    virtio_queue_notify(&g_dev, VQ_RX);

    return (int)data_len;
}

// Расширенная диагностика: что QEMU видит в common_cfg и config.
void virtio_net_debug2(void) {
    serial_print("[dbg2] status=");
    serial_print_hex(g_dev.cfg->device_status);
    serial_print("\n");

    g_dev.cfg->queue_select = VQ_RX;
    __asm__ volatile ("mfence" ::: "memory");
    serial_print("[dbg2] RX en=");
    serial_print_hex(g_dev.cfg->queue_enable);
    serial_print(" drv=");
    serial_print_hex((unsigned int)g_dev.cfg->queue_driver);
    serial_print(" dev=");
    serial_print_hex((unsigned int)g_dev.cfg->queue_device);
    serial_print(" desc=");
    serial_print_hex((unsigned int)g_dev.cfg->queue_desc);
    serial_print("\n");

    serial_print("[dbg2] RX desc[0] addr=");
    serial_print_hex((unsigned int)g_dev.queues[VQ_RX].desc[0].addr);
    serial_print(" len=");
    serial_print_int((int)g_dev.queues[VQ_RX].desc[0].len);
    serial_print(" flags=");
    serial_print_hex(g_dev.queues[VQ_RX].desc[0].flags);
    serial_print("\n");
    serial_print("[dbg2] RX avail idx=");
    serial_print_int((int)g_dev.queues[VQ_RX].avail->idx);
    serial_print(" ring[0]=");
    serial_print_int((int)g_dev.queues[VQ_RX].avail->ring[0]);
    serial_print(" ring[15]=");
    serial_print_int((int)g_dev.queues[VQ_RX].avail->ring[15]);
    serial_print("\n");
}

// Диагностика: напечатать в serial адреса и индексы очередей.
void virtio_net_debug(void) {
    serial_print("[dbg] cfg=");
    serial_print_hex((unsigned int)g_dev.cfg);
    serial_print(" notify_base=");
    serial_print_hex((unsigned int)g_dev.notify_base);
    serial_print(" mult=");
    serial_print_int((int)g_dev.notify_off_multiplier);
    serial_print(" device_cfg=");
    serial_print_hex((unsigned int)g_dev.device_cfg);
    serial_print("\n");
    serial_print("[dbg] RX avail_idx=");
    serial_print_int((int)g_dev.queues[VQ_RX].avail->idx);
    serial_print(" used_idx=");
    serial_print_int((int)g_dev.queues[VQ_RX].used->idx);
    serial_print(" last_used=");
    serial_print_int((int)g_dev.queues[VQ_RX].last_used_idx);
    serial_print("\n");
    serial_print("[dbg] TX avail_idx=");
    serial_print_int((int)g_dev.queues[VQ_TX].avail->idx);
    serial_print(" used_idx=");
    serial_print_int((int)g_dev.queues[VQ_TX].used->idx);
    serial_print(" last_used=");
    serial_print_int((int)g_dev.queues[VQ_TX].last_used_idx);
    serial_print("\n");
}

// ============================================================
// Регистрация в VDS
// ============================================================
static const driver_t VIRTIO_NET_DRIVER = {
    "virtio-net",
    DRV_NET,
    DRV_LEVEL_NORMAL,
    virtio_net_match,
    virtio_net_probe,
    0
};

void virtio_net_init(void) {
    driver_register(&VIRTIO_NET_DRIVER);
}
