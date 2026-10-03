// tcp.h - минимальный TCP-клиент.
//
// Состояния соединения (см. RFC 793):
//   CLOSED -> SYN_SENT -> ESTABLISHED -> FIN_WAIT -> CLOSED
// Реализован только клиент, только активное открытие.
//
// Особенности:
//   - Один сегмент в полёте за раз (без окна > 1).
//   - Нет retransmit (QEMU/интернет не теряют в LAN).
//   - Нет SACK, timestamp, window scale.
//   - MSS negotiation — QEMU не требует, мы всё равно посылаем.
#ifndef TCP_H
#define TCP_H

#include "net.h"

// Заголовок TCP (20 байт без опций).
typedef struct {
    n_u16 src_port;    // big-endian
    n_u16 dst_port;    // big-endian
    n_u32 seq;         // big-endian
    n_u32 ack;         // big-endian
    n_u8  data_off;    // 4 бита: размер заголовка в 32-битных словах
    n_u8  flags;       // CWR/ECE/URG/ACK/PSH/RST/SYN/FIN
    n_u16 window;      // big-endian
    n_u16 checksum;    // big-endian
    n_u16 urgent;      // big-endian
} __attribute__((packed)) tcp_hdr_t;

// Флаги
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

// Состояния
typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT,
    TCP_TIME_WAIT,
} tcp_state_t;

// Соединение
typedef struct {
    tcp_state_t   state;
    unsigned char dst_ip[4];
    unsigned short dst_port;      // host order
    unsigned short src_port;      // host order
    unsigned int  send_seq;       // наш следующий seq
    unsigned int  recv_ack;       // что мы должны получить от другой стороны
    unsigned int  last_ack_from_peer;   // их ack на наши данные
} tcp_conn_t;

// --- API ---

// Открыть TCP-соединение. Блокирующий: ждёт SYN-ACK до ~3 секунд.
// Возвращает 0 при успехе, -1 при ошибке.
int  net_tcp_connect(tcp_conn_t* c, const unsigned char* ip, unsigned short port);

// Отправить данные. Возвращает число байт или -1.
int  net_tcp_send(tcp_conn_t* c, const void* data, unsigned int len);

// Принять данные в буфер. Возвращает число байт (0 если пусто) или -1.
int  net_tcp_recv(tcp_conn_t* c, void* buf, unsigned int max);

// Закрыть соединение (FIN).
void net_tcp_close(tcp_conn_t* c);

// Внутренняя: обработка входящего TCP-сегмента. Зовётся из handle_ip.
void tcp_handle_segment(const unsigned char* ip_src, const unsigned char* p, unsigned int len);

#endif // TCP_H
