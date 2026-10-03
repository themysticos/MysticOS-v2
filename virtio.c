// virtio.c - базовый слой VirtIO 1.2 (modern PCI).
//
// См. virtio.h — там описание модели.
//
// Поддерживается ТОЛЬКО modern (device id 0x1040-0x107F):
//   virtio-net:  0x1041 (modern), 0x1000 (legacy/transitional)
//   virtio-gpu:  0x1050 (modern), 0x1010 (legacy/transitional)
//   virtio-blk:  0x1042 (modern), 0x1001 (legacy/transitional)
//
// QEMU с -device virtio-net-pci по умолчанию даёт legacy/transitional
// (device id 0x1000), НО у него ЕСТЬ modern capabilities. Мы будем
// искать capabilities по PCI config — они есть и у transitional устройств.
#include "virtio.h"
#include "pci.h"
#include "serial.h"
#include "io.h"

// ============================================================
// Поиск устройства
// ============================================================
static int find_cb(int bus, int slot, int func,
                   unsigned int vendor, unsigned int device,
                   unsigned int class_code) {
    (void)class_code;
    extern unsigned int g_find_device_id;
    extern virtio_dev_t* g_find_out;
    if (vendor != VIRTIO_PCI_VENDOR_ID) return 1;
    if (device != g_find_device_id) return 1;
    if (!g_find_out) return 0;
    g_find_out->present = 1;
    g_find_out->pci_vendor = vendor;
    g_find_out->pci_device = device;
    g_find_out->pci_bus = bus;
    g_find_out->pci_slot = slot;
    g_find_out->pci_func = func;
    return 0;
}

unsigned int g_find_device_id = 0;
virtio_dev_t* g_find_out = 0;

int virtio_find_pci(v_u16 device_id, virtio_dev_t* out) {
    for (int i = 0; i < (int)sizeof(virtio_dev_t); i++) ((unsigned char*)out)[i] = 0;
    g_find_device_id = device_id;
    g_find_out = out;
    pci_enumerate(find_cb);
    g_find_out = 0;
    return out->present;
}

// ============================================================
// PCI capabilities: найти COMMON/NOTIFY/DEVICE
// ============================================================
// Capabilities находятся в PCI config space. Стандартная цепочка:
//   offset 0x34 -> первый cap. cap[0] = id, cap[1] = next.
// Каждый VIRTIO_PCI_CAP_VENDOR (id=0x09) содержит:
//   +3 cfg_type (1=COMMON, 2=NOTIFY, 3=ISR, 4=DEVICE)
//   +4 bar, +8 offset, +12 length.
static v_u32 pci_bar_phys(int bus, int slot, int func, int bar_idx) {
    unsigned int lo, hi, sz;
    int is_io, is_64;
    if (!pci_bar_info(bus, slot, func, bar_idx, &lo, &hi, &sz, &is_io, &is_64)) return 0;
    (void)hi; (void)sz; (void)is_io; (void)is_64;
    return lo;
}

static int virtio_map_caps(virtio_dev_t* dev) {
    int bus = dev->pci_bus, slot = dev->pci_slot, func = dev->pci_func;
    // Статус: bit 4 (Capabilities List) должен быть 1.
    unsigned short status = pci_read16(bus, slot, func, 0x06);
    if (!(status & 0x10)) {
        serial_print("[virtio] no capability list\n");
        return 0;
    }
    unsigned char cap_off = pci_read8(bus, slot, func, 0x34) & 0xFC;
    int guard = 0;
    while (cap_off && guard++ < 48) {
        unsigned char id = pci_read8(bus, slot, func, cap_off + 0);
        unsigned char next = pci_read8(bus, slot, func, cap_off + 1) & 0xFC;
        if (id == VIRTIO_PCI_CAP_VENDOR) {
            unsigned char cfg_type = pci_read8(bus, slot, func, cap_off + 3);
            unsigned char bar = pci_read8(bus, slot, func, cap_off + 4);
            unsigned int off = pci_read32(bus, slot, func, cap_off + 8);
            unsigned int len = pci_read32(bus, slot, func, cap_off + 12);
            unsigned int bar_phys = pci_bar_phys(bus, slot, func, bar);
            unsigned int addr = bar_phys + off;
            switch (cfg_type) {
            case VIRTIO_PCI_CAP_COMMON:
                dev->common_bar = bar;  dev->common_off = off; dev->common_len = len;
                dev->cfg = (volatile virtio_pci_common_cfg_t*)addr;
                break;
            case VIRTIO_PCI_CAP_NOTIFY: {
                dev->notify_bar = bar;  dev->notify_off = off; dev->notify_len = len;
                dev->notify_base = (volatile v_u8*)addr;
                // notify_off_multiplier идёт СРАЗУ за length (по смещению 16
                // от начала cap). См. VirtIO 1.2, 4.1.4.4.
                unsigned int mult = pci_read32(bus, slot, func, cap_off + 16);
                dev->notify_off_multiplier = mult;
                serial_print("[virtio] notify off_mult=");
                serial_print_int((int)mult);
                serial_print("\n");
                break;
            }
            case VIRTIO_PCI_CAP_DEVICE:
                dev->device_bar = bar;  dev->device_off = off; dev->device_len = len;
                dev->device_cfg = (volatile v_u8*)addr;
                break;
            default:
                break;   // ISR, PCI_CFG — не нужны сейчас
            }
        }
        cap_off = next;
    }
    return dev->cfg ? 1 : 0;
}

// ============================================================
// init
// ============================================================
static void set_status(virtio_dev_t* dev, v_u8 bits) {
    dev->cfg->device_status = bits;
}
static v_u8 get_status(virtio_dev_t* dev) {
    return dev->cfg->device_status;
}

int virtio_init(virtio_dev_t* dev, v_u64 wanted_features) {
    if (!virtio_map_caps(dev)) return 0;

    // 1. Сброс.
    set_status(dev, 0);

    // 2. ACK + DRIVER.
    v_u8 s = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    set_status(dev, s);

    // 3. Читаем device features (первые 64 бита).
    dev->cfg->device_feature_select = 0;
    v_u32 f_lo = dev->cfg->device_feature;
    dev->cfg->device_feature_select = 1;
    v_u32 f_hi = dev->cfg->device_feature;
    v_u64 dev_features = ((v_u64)f_hi << 32) | f_lo;

    // 4. Negotiate: хотим = wanted & device_features.
    v_u64 agreed = wanted_features & dev_features;
    // VIRTIO_F_VERSION_1 (bit 32) обязателен для modern.
    // QEMU transitional всегда даёт его в hi-word.
    dev->negotiated_features = agreed;

    dev->cfg->driver_feature_select = 0;
    dev->cfg->driver_feature = (v_u32)(agreed & 0xFFFFFFFFu);
    dev->cfg->driver_feature_select = 1;
    dev->cfg->driver_feature = (v_u32)((agreed >> 32) & 0xFFFFFFFFu);

    // 5. FEATURES_OK.
    s |= VIRTIO_STATUS_FEATURES_OK;
    set_status(dev, s);
    if (!(get_status(dev) & VIRTIO_STATUS_FEATURES_OK)) {
        serial_print("[virtio] FEATURES_OK failed\n");
        set_status(dev, VIRTIO_STATUS_FAILED);
        return 0;
    }

    // 6. Сколько очередей у устройства.
    dev->num_queues = dev->cfg->num_queues;
    serial_print("[virtio] num_queues=");
    serial_print_int(dev->num_queues);
    serial_print(" features=");
    serial_print_hex((unsigned int)(agreed & 0xFFFFFFFFu));
    serial_print(":");
    serial_print_hex((unsigned int)((agreed >> 32) & 0xFFFFFFFFu));
    serial_print("\n");

    // 7. DRIVER_OK НЕ ставим тут! По спецификации VirtIO 1.2 (§3.1.1):
    // "The driver MUST configure the device's virtqueues before setting
    //  the DRIVER_OK status bit." Иначе QEMU не обрабатывает RX.
    // DRIVER_OK ставится отдельным вызовом virtio_driver_ok() после
    // настройки всех очередей.
    return 1;
}

void virtio_driver_ok(virtio_dev_t* dev) {
    v_u8 s = get_status(dev);
    s |= VIRTIO_STATUS_DRIVER_OK;
    dev->cfg->device_status = s;
    __asm__ volatile ("mfence" ::: "memory");
}

// ============================================================
// queue setup
// ============================================================
int virtio_queue_setup(virtio_dev_t* dev, int idx, v_u16 size,
                       void* desc, void* avail, void* used) {
    if (idx < 0 || idx >= VIRTIO_MAX_QUEUES) return 0;
    virtqueue_t* q = &dev->queues[idx];
    if (q->in_use) return 0;

    dev->cfg->queue_select = (v_u16)idx;
    v_u16 max_size = dev->cfg->queue_size;
    if (max_size == 0) {
        serial_print("[virtio] queue " );
        serial_print_int(idx);
        serial_print(" max_size=0\n");
        return 0;
    }
    if (size > max_size) size = max_size;
    // размер должен быть степенью двойки (для индексации через маску)
    // Округлим вниз до степени двойки.
    while (size & (size - 1)) size &= size - 1;
    if (size < 2) size = 2;

    q->queue_size = size;
    q->queue_notify_off = dev->cfg->queue_notify_off;
    q->desc = (virtq_desc_t*)desc;
    q->avail = (virtq_avail_t*)avail;
    q->used = (virtq_used_t*)used;
    q->last_used_idx = 0;
    q->next_desc = 0;

    // Обнулить desc/avail/used.
    for (v_u32 i = 0; i < (v_u32)size * sizeof(virtq_desc_t); i++) ((v_u8*)desc)[i] = 0;
    for (v_u32 i = 0; i < sizeof(virtq_avail_t) + (v_u32)size * 2u; i++) ((v_u8*)avail)[i] = 0;
    for (v_u32 i = 0; i < sizeof(virtq_used_t) + (v_u32)size * sizeof(virtq_used_elem_t); i++) ((v_u8*)used)[i] = 0;

    q->desc_phys  = (v_u64)(v_u32)desc;
    q->avail_phys = (v_u64)(v_u32)avail;
    q->used_phys  = (v_u64)(v_u32)used;

    // Сообщить устройству физические адреса.
    dev->cfg->queue_size  = size;
    dev->cfg->queue_desc  = q->desc_phys;
    dev->cfg->queue_driver = q->avail_phys;
    dev->cfg->queue_device = q->used_phys;
    dev->cfg->queue_enable = 1;

    q->in_use = 1;
    return 1;
}

// ============================================================
// queue add / notify / wait
// ============================================================
int virtio_queue_add(virtio_dev_t* dev, int idx,
                     v_u64 addr, v_u32 len, v_u16 flags) {
    virtqueue_t* q = &dev->queues[idx];
    if (!q->in_use) return -1;
    v_u16 d = q->next_desc;
    q->desc[d].addr  = addr;
    q->desc[d].len   = len;
    q->desc[d].flags = flags;
    q->desc[d].next  = 0;
    q->next_desc = (d + 1) & (q->queue_size - 1);
    // avail ring: записать d, увеличить idx.
    v_u16 a = q->avail->idx & (q->queue_size - 1);
    q->avail->ring[a] = d;
    // memory barrier: сначала ring, потом idx.
    __asm__ volatile ("" ::: "memory");
    q->avail->idx++;
    return (int)d;
}

void virtio_queue_notify(virtio_dev_t* dev, int idx) {
    virtqueue_t* q = &dev->queues[idx];
    if (!q->in_use) return;
    // notify_off_multiplier из PCI cap (обычно 2 или 4). Раньше был хардкод
    // * 2 — неверно для QEMU (там обычно 4), notify не доходил.
    v_u32 mult = dev->notify_off_multiplier ? dev->notify_off_multiplier : 2;
    v_u32 off = q->queue_notify_off * mult;
    *(volatile v_u16*)(dev->notify_base + off) = (v_u16)idx;
}

// НЕБЛОКИРУЮЩИЙ: вернуть -1 если пакета нет.
// Раньше тут было 100M итераций pause — это вешало ядро на секунды.
// Правильно: poll в главном цикле или IRQ.
int virtio_queue_wait(virtio_dev_t* dev, int idx, v_u32* out_len) {
    virtqueue_t* q = &dev->queues[idx];
    if (!q->in_use) return -1;
    if (q->used->idx == q->last_used_idx) return -1;   // нет данных
    v_u16 slot = q->last_used_idx & (q->queue_size - 1);
    v_u32 id = q->used->ring[slot].id;
    v_u32 len = q->used->ring[slot].len;
    q->last_used_idx++;
    if (out_len) *out_len = len;
    return (int)id;
}
