// virtio_net.h - драйвер virtio-net (2D, без offload).
//
// Использует virtio.c (транспорт). Регистрируется в VDS через
// virtio_net_init(). Найденное устройство (device_id 0x1000 или 0x1041,
// vendor 0x1AF4) вызывается probe -> инициализация очередей -> готов.
#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "driver.h"

// Зарегистрировать драйвер в VDS. Звать в kernel_main до driver_scan.
void virtio_net_init(void);

// match/probe для driver_t. Экспортируем — нужны в driver.c? Нет.
// Но пусть будут в driver_scan через registed vtable.

// --- API драйвера (для будущего net.c) ---

// MAC-адрес (6 байт, валиден после probe). 0 если драйвер не поднят.
const unsigned char* virtio_net_mac(void);

// Отправить ethernet-фрейм (без virtio_net_hdr). Вернёт число байт или -1.
int virtio_net_send(const void* data, unsigned int len);

// Принять ethernet-фрейм в buf (до max байт). Вернёт длину или -1 если
// нет пакета.
int virtio_net_recv(void* buf, unsigned int max);

// Готов ли драйвер (probe прошёл успешно).
int virtio_net_ready(void);

// Диагностика в serial: адреса, индексы очередей.
void virtio_net_debug(void);
void virtio_net_debug2(void);

#endif // VIRTIO_NET_H
