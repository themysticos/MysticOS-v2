// driver.h - VDS: Virtual Driver System для MysticOS 2.
//
// Реестр драйверов. Драйвер регистрирует себя (driver_register),
// ядро сканирует шины (driver_scan) и вызывает probe для совпавших
// устройств. Ядру НЕ нужно знать, какие драйверы есть и как они
// работают — оно зовёт общий API.
//
// Модель аналог Linux: struct device_driver + struct pci_driver.
// Аналогия в этом же проекте: vfs.c (fs_driver_t) — та же идея
// для файловых систем.
//
// Классы: NET, GPU, BLK, INPUT, OTHER.
// Уровни: EARLY (PCI, консоль), NORMAL (сеть, GPU), LATE (USB).
#ifndef DRIVER_H
#define DRIVER_H

#define DRV_MAX_DRIVERS   32

// Классы устройств
#define DRV_OTHER   0
#define DRV_NET     1
#define DRV_GPU     2
#define DRV_BLK     3
#define DRV_INPUT   4

// Уровни инициализации
#define DRV_LEVEL_EARLY   0
#define DRV_LEVEL_NORMAL  1
#define DRV_LEVEL_LATE    2

// Шины
#define BUS_PCI     0
#define BUS_ISA     1
#define BUS_USB     2
#define BUS_VIRTIO  3   // virtio-mmio (не PCI)

// Одно устройство (в текущей версии — PCI; позже — USB/ISA).
typedef struct device_s {
    int          bus;            // BUS_*
    int          bus_num;        // PCI bus number
    int          slot;           // PCI device (slot)
    int          func;           // PCI function
    unsigned int vendor;         // PCI vendor id (0x1AF4 = VirtIO)
    unsigned int device;         // PCI device id
    unsigned int class_code;     // PCI class (0x02xxxx = network)
    unsigned int bar[6];         // BARs 0..5 (физические адреса)
    int          irq;            // IRQ line (или -1)
    void*        priv;           // private data драйвера
} device_t;

// Один драйвер.
typedef struct driver_s {
    const char*  name;           // "virtio-net", "rtl8723de", "vbe-gpu"
    int          class;          // DRV_*
    int          level;          // DRV_LEVEL_*
    // match: вернуть 1, если драйвер умеет работать с этим устройством.
    int (*match)(const device_t* dev);
    // probe: инициализировать устройство. 0 = успех, иначе — ошибка
    // (ядро не будет звать remove, если probe вернул ошибку).
    int (*probe)(device_t* dev);
    // remove: выгрузить драйвер (опционально).
    void (*remove)(device_t* dev);
} driver_t;

// --- API ---

// Очистить реестр. Звать один раз при старте.
void driver_init(void);

// Зарегистрировать драйвер. Возвращает 1 при успехе, 0 если нет места.
int  driver_register(const driver_t* drv);

// Пройти по шинам (сейчас — только PCI), собрать device_t, для каждого
// найти match-драйвер и позвать probe. Возвращает число успешных probe.
int  driver_scan(void);

// Сколько драйверов зарегистрировано.
int  driver_count(void);

// Получить драйвер по индексу (для отладочной печати).
const driver_t* driver_get(int i);

// Найти зарегистрированный драйвер по имени (для отладки).
const driver_t* driver_find(const char* name);

#endif // DRIVER_H
