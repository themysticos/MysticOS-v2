// ms/net.h - сетевой API для программ MysticOS.
//
// Программа может узнать состояние сети: готова ли, есть ли IP,
// счётчики трафика. Для будущего браузера / чата / HTTP-клиента.
#ifndef MS_NET_H
#define MS_NET_H

#include "syscall.h"

// Статус сети (совпадает с net_status_t в ядре).
typedef struct {
    int ready;
    int dhcp_done;
    int have_ip;
    unsigned char ip[4];
    unsigned char gw[4];
    unsigned char dns[4];
    unsigned int rx_packets;
    unsigned int tx_packets;
} ms_net_status_t;

// Заполнить структуру текущим состоянием сети.
static inline void ms_net_status(ms_net_status_t* out) {
    __ms_sc1(SYS_NET_STATUS, (unsigned int)out);
}

#endif // MS_NET_H
