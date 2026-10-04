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

// --- Асинхронный HTTP ---
// 1) начать: ms_http_start(url) -> 0/-1
// 2) крутить свой цикл (ms_yield), периодически опрашивая ms_http_poll():
//      0 = ещё грузится, 1 = готово, -1 = ошибка
// 3) забрать тело: ms_http_result(buf, max) -> длина или -1
static inline int ms_http_start(const char* url) {
    return __ms_sc1(SYS_HTTP_START, (unsigned int)url);
}
static inline int ms_http_poll(void) {
    return __ms_sc0(SYS_HTTP_POLL);
}
static inline int ms_http_result(char* buf, unsigned int max) {
    return __ms_sc2(SYS_HTTP_RESULT, (unsigned int)buf, max);
}

#endif // MS_NET_H
