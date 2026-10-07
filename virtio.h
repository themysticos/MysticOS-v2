// virtio.h - базовый слой VirtIO (v1.2, modern + legacy).
//
// Реализация для MysticOS 2. Используется для GPU, сети, диска.
// Сейчас — только минимум для virtio-gpu.
//
// Термины:
//   * Device — виртуальное устройство (GPU, net, blk).
//   * Virtqueue — очередь сообщений (ring + descriptor table + avail + used).
//   * Descriptor — ссылка на буфер в памяти гостя (RAM).
//   * Driver/Device features — что умеет каждая сторона.
//
// Соглашение: только modern (MMIO/PCI с VIRTIO_PCI_CAP_*).
// Legacy (порт 0x1F0-подобный) НЕ поддерживается.
#ifndef VIRTIO_H
#define VIRTIO_H

typedef unsigned char  v_u8;
typedef unsigned short v_u16;
typedef unsigned int   v_u32;
typedef unsigned long long v_u64;

// ============================================================
// PCI Vendor/Device ID для VirtIO
// ============================================================
#define VIRTIO_PCI_VENDOR_ID  0x1AF4
// Device IDs:
//   0x1000-0x103F — legacy (transitional)
//   0x1040-0x107F — modern (non-transitional)
// Для GPU: 0x1050 (modern), 0x1010 (legacy/transitional)

// ============================================================
// VirtIO PCI Capabilities (common config, notify, ISR, device)
// ============================================================
#define VIRTIO_PCI_CAP_VENDOR   0x09   // PCI vendor-specific capability
#define VIRTIO_PCI_CAP_COMMON   1      // struct virtio_pci_common_cfg
#define VIRTIO_PCI_CAP_NOTIFY   2      // notify region
#define VIRTIO_PCI_CAP_ISR      3      // ISR status
#define VIRTIO_PCI_CAP_DEVICE   4      // device-specific config
#define VIRTIO_PCI_CAP_PCI_CFG  5      // access via PCI config space

typedef struct {
    v_u8  cap_vndr;    // = VIRTIO_PCI_CAP_VENDOR
    v_u8  cap_next;    // next PCI cap offset
    v_u8  cap_len;     // length of this cap
    v_u8  cfg_type;    // VIRTIO_PCI_CAP_*
    v_u8  bar;         // which BAR (0..5)
    v_u8  padding[3];
    v_u32 offset;      // offset within BAR
    v_u32 length;      // size of region
} __attribute__((packed)) virtio_pci_cap_t;

// ============================================================
// Common config (virtio_pci_common_cfg)
// ============================================================
typedef struct {
    v_u32 device_feature_select;   // ro-ish: driver выбирает, какие features читать
    v_u32 device_feature;          // ro: возможности устройства
    v_u32 driver_feature_select;   // rw: driver выбирает, какие features писать
    v_u32 driver_feature;          // rw: что включил driver
    v_u16 msix_config;             // rw: MSI-X vector для config changes
    v_u16 num_queues;              // ro: сколько очередей у устройства
    v_u8  device_status;           // rw: состояние (ACK/DRIVER/FEATURES_OK/...)
    v_u8  config_generation;       // ro: меняется при изменении config
    v_u16 queue_select;            // rw: какую очередь настраиваем
    v_u16 queue_size;              // rw: размер очереди (макс)
    v_u16 queue_msix_vector;       // rw: MSI-X vector для этой очереди
    v_u16 queue_enable;            // rw: 1 = включить очередь
    v_u16 queue_notify_off;        // ro: смещение для notify
    v_u64 queue_desc;              // rw: физ. адрес descriptor table
    v_u64 queue_driver;            // rw: физ. адрес avail ring
    v_u64 queue_device;            // rw: физ. адрес used ring
} __attribute__((packed)) virtio_pci_common_cfg_t;

// ============================================================
// Device status bits
// ============================================================
#define VIRTIO_STATUS_ACKNOWLEDGE  1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4
#define VIRTIO_STATUS_FEATURES_OK  8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED       128

// ============================================================
// Virtqueue (split ring, v1.2)
// ============================================================
// Descriptor — 16 байт. Один буфер (или chain через next).
typedef struct {
    v_u64 addr;    // физ. адрес буфера
    v_u32 len;     // длина
    v_u16 flags;   // VIRTQ_DESC_F_*
    v_u16 next;    // следующий дескриптор (если F_NEXT)
} __attribute__((packed)) virtq_desc_t;

#define VIRTQ_DESC_F_NEXT   1   // chain: продолжение в next
#define VIRTQ_DESC_F_WRITE  2   // device пишет в этот буфер (для output)
#define VIRTQ_DESC_F_INDIRECT 4

// Avail ring: driver -> device. Каждая запись — индекс дескриптора.
typedef struct {
    v_u16 flags;
    v_u16 idx;
    v_u16 ring[];   // размер = queue_size (число дескрипторов)
} __attribute__((packed)) virtq_avail_t;

// Used ring: device -> driver. Каждая запись — id + длина ответа.
typedef struct {
    v_u32 id;
    v_u32 len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
    v_u16 flags;
    v_u16 idx;
    virtq_used_elem_t ring[];
} __attribute__((packed)) virtq_used_t;

#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

// Runtime-состояние одной очереди.
typedef struct {
    int             in_use;       // 1 = уже настроена
    v_u16           queue_size;   // размер (число дескрипторов)
    v_u16           queue_notify_off;

    virtq_desc_t*   desc;         // descriptor table (выровнено 16)
    virtq_avail_t*  avail;        // avail ring (выровнено 2)
    virtq_used_t*   used;         // used ring (выровнено 4)

    v_u16           last_used_idx;   // сколько used-элементов обработали
    v_u16           next_desc;       // следующий свободный дескриптор (для push)

    // Физические адреса (для common_cfg)
    v_u64           desc_phys;
    v_u64           avail_phys;
    v_u64           used_phys;
} virtqueue_t;

// ============================================================
// VirtIO-устройство (одно)
// ============================================================
typedef struct {
    int             present;
    v_u16           pci_vendor;
    v_u16           pci_device;
    v_u8            pci_bus, pci_slot, pci_func;

    // BAR / MMIO offsets (из PCI capabilities)
    v_u8            common_bar;
    v_u32           common_off;
    v_u32           common_len;
    v_u8            notify_bar;
    v_u32           notify_off;
    v_u32           notify_len;
    v_u32           notify_off_multiplier;   // из NOTIFY cap (обычно 2 или 4)
    v_u8            device_bar;
    v_u32           device_off;
    v_u32           device_len;

    volatile virtio_pci_common_cfg_t* cfg;   // mapped common config
    volatile v_u8*  notify_base;               // mapped notify region
    volatile v_u8*  device_cfg;                // mapped device-specific

    v_u64           negotiated_features;

    // Очереди (у GPU их обычно 2: controlq=0, cursorq=1)
#define VIRTIO_MAX_QUEUES 4
    virtqueue_t     queues[VIRTIO_MAX_QUEUES];
    v_u16           num_queues;
} virtio_dev_t;

// ============================================================
// API
// ============================================================

// Найти virtio-устройство по PCI device id (например 0x1050 для gpu).
// Возвращает 1 если найдено и записано в *out.
int  virtio_find_pci(v_u16 device_id, virtio_dev_t* out);

// Инициализировать устройство (ACK/DRIVER, negotiate features, FEATURES_OK).
// DRIVER_OK НЕ ставится — вызывающий должен настроить очереди и затем
// вызвать virtio_driver_ok(). Это требование спецификации VirtIO.
int  virtio_init(virtio_dev_t* dev, v_u64 wanted_features);

// Установить DRIVER_OK. Звать ПОСЛЕ virtio_queue_setup для всех очередей.
void virtio_driver_ok(virtio_dev_t* dev);

// Настроить очередь #idx, размер size. Возвращает 1 при успехе.
// desc/avail/used должны быть в уже выделенной памяти (например, pmem).
int  virtio_queue_setup(virtio_dev_t* dev, int idx, v_u16 size,
                        void* desc, void* avail, void* used);

// Отправить один дескриптор (addr, len, flags) в очередь #idx.
// Возвращает индекс дескриптора или -1.
int  virtio_queue_add(virtio_dev_t* dev, int idx,
                      v_u64 addr, v_u32 len, v_u16 flags);

// Добавить цепочку дескрипторов (команда out + ответ in для virtio-gpu).
// Возвращает индекс первого дескриптора или -1.
int  virtio_queue_add_chain(virtio_dev_t* dev, int idx,
                            v_u64* addrs, v_u32* lens, v_u16* flags, int n);

// Оповестить устройство, что в очереди есть новые дескрипторы.
void virtio_queue_notify(virtio_dev_t* dev, int idx);

// Ждать, пока устройство обработает хотя бы один элемент. Возвращает id элемента.
int  virtio_queue_wait(virtio_dev_t* dev, int idx, v_u32* out_len);

#endif // VIRTIO_H
