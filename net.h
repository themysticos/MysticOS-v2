// net.h - сетевой стек MysticOS 2: Ethernet, ARP, IPv4, ICMP.
//
// Использует драйвер virtio_net (или любой другой с API send/recv).
// Модель: net_poll() вызывается в главном цикле ядра, читает пакеты из
// драйвера, обрабатывает, отвечает. Отправка — прямая.
#ifndef NET_H
#define NET_H

typedef unsigned char  n_u8;
typedef unsigned short n_u16;
typedef unsigned int   n_u32;

// ============================================================
// Ethernet (14 байт)
// ============================================================
#define ETH_ALEN    6
typedef struct {
    n_u8 dst[6];
    n_u8 src[6];
    n_u16 ethertype;   // big-endian на проводе
} __attribute__((packed)) eth_hdr_t;

#define ETH_HTONS(x)  ((n_u16)((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF)))

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

// ============================================================
// ARP (28 байт для IPv4)
// ============================================================
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

typedef struct {
    n_u16 htype;    // 1 = Ethernet
    n_u16 ptype;    // 0x0800 = IPv4
    n_u8  hlen;     // 6
    n_u8  plen;     // 4
    n_u16 op;       // ARP_OP_*
    n_u8  sha[6];   // sender MAC
    n_u8  spa[4];   // sender IP
    n_u8  tha[6];   // target MAC
    n_u8  tpa[4];   // target IP
} __attribute__((packed)) arp_hdr_t;

// ============================================================
// IPv4 (20 байт без опций)
// ============================================================
typedef struct {
    n_u8  ver_ihl;      // 0x45 (IPv4, IHL=5)
    n_u8  tos;
    n_u16 total_len;    // big-endian
    n_u16 id;
    n_u16 frag_off;
    n_u8  ttl;
    n_u8  proto;        // 1=ICMP, 6=TCP, 17=UDP
    n_u16 checksum;
    n_u8  src[4];
    n_u8  dst[4];
} __attribute__((packed)) ip_hdr_t;

#define IP_PROTO_ICMP  1
#define IP_PROTO_UDP   17
#define IP_PROTO_TCP   6

// ============================================================
// ICMP
// ============================================================
#define ICMP_ECHO_REQUEST  8
#define ICMP_ECHO_REPLY    0

typedef struct {
    n_u8  type;
    n_u8  code;
    n_u16 checksum;
    n_u16 id;
    n_u16 seq;
    // далее — payload (для echo — те же байты что в request)
} __attribute__((packed)) icmp_hdr_t;

// ============================================================
// UDP (8 байт заголовок)
// ============================================================
typedef struct {
    n_u16 src_port;    // big-endian
    n_u16 dst_port;    // big-endian
    n_u16 length;      // big-endian: 8 + payload_len
    n_u16 checksum;    // big-endian; 0 = не считать (IPv4 разрешает)
} __attribute__((packed)) udp_hdr_t;

// ============================================================
// Наш IP/MAC
// ============================================================

// Инициализация. Устанавливает MAC (из драйвера), ставит статический IP
// (по умолчанию 10.0.2.15 — QEMU), gateway, маску.
// MAC берётся из virtio_net_mac().
void net_init(void);

// Взять MAC у драйвера (вместо net_init).
void net_set_mac(const unsigned char* mac);

// Установить IP/gateway/mask вручную.
void net_set_ip(const unsigned char* ip, const unsigned char* gw, const unsigned char* mask);

// Обработать один входящий пакет из драйвера (если есть). Звать в цикле.
// Возвращает 1, если обработали пакет, 0 — если нет данных.
int net_poll(void);

// Отправить ICMP echo (ping). Возвращает 0 при успехе.
int net_ping(const unsigned char* ip);

// Отправить UDP-датаграмму. Возвращает 0 или -1.
int net_udp_send(const unsigned char* dst_ip, unsigned short src_port,
                 unsigned short dst_port, const void* data, unsigned int len);

// Запустить DHCP: отправить DISCOVER. Результат придёт в обработчике
// UDP (handle_udp) — по получении ACK установятся g_ip/g_gw/g_mask.
// Возвращает 0 или -1.
int net_dhcp_discover(void);

// Готов ли DHCP (получен ACK).
int net_dhcp_done(void);

// Забрать следующий принятый UDP-пакет. Возвращает длину или -1.
// src_ip — куда положить IP отправителя (4 байта), src_port — порт.
// Идёт поверх очереди из 4 слотов, приёмник читает в порядке FIFO.
int net_udp_recv(unsigned char* src_ip, unsigned short* src_port,
                 void* buf, unsigned int max);

// Наш MAC/IP (для отладки).
const unsigned char* net_our_mac(void);
const unsigned char* net_our_ip(void);

// Отладка в serial.
void net_dump(void);

#endif // NET_H
