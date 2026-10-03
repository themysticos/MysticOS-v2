// net.c - сетевой стек: Ethernet, ARP, IPv4, ICMP.
#include "net.h"
#include "virtio_net.h"
#include "serial.h"

// ============================================================
// Наши адреса
// ============================================================
static unsigned char g_mac[6] = {0,0,0,0,0,0};
static unsigned char g_ip[4]  = {10, 0, 2, 15};    // QEMU по умолчанию
static unsigned char g_gw[4]  = {10, 0, 2, 2};
static unsigned char g_mask[4]= {255, 255, 255, 0};

// ARP-таблица (упрощённая, 8 записей)
#define ARP_TABLE_SIZE 8
typedef struct {
    unsigned char ip[4];
    unsigned char mac[6];
    int valid;
} arp_entry_t;
static arp_entry_t g_arp[ARP_TABLE_SIZE];

// Буферы для отправки/приёма
static unsigned char g_txbuf[2048];
static unsigned char g_rxbuf[2048];

// Внутренние номера для ICMP id/seq
static n_u16 g_icmp_id = 0x1234;
static n_u16 g_icmp_seq = 0;

// ============================================================
// Утилиты: big-endian (сетевой порядок)
// ============================================================
static n_u16 be16(n_u16 x) { return (n_u16)((x << 8) | (x >> 8)); }
static n_u32 be32(n_u32 x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8)
         | ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

static int ip_eq(const unsigned char* a, const unsigned char* b) {
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}
static int ip_zero(const unsigned char* a) {
    return a[0]==0 && a[1]==0 && a[2]==0 && a[3]==0;
}

static unsigned short ip_checksum(const void* data, unsigned int len) {
    const n_u16* p = (const n_u16*)data;
    unsigned int sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const n_u8*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (n_u16)(~sum);
}

static int mac_eq(const unsigned char* a, const unsigned char* b) {
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int mac_broadcast(const unsigned char* a) {
    for (int i = 0; i < 6; i++) if (a[i] != 0xFF) return 0;
    return 1;
}

// ============================================================
// ARP table
// ============================================================
static arp_entry_t* arp_find(const unsigned char* ip) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++)
        if (g_arp[i].valid && ip_eq(g_arp[i].ip, ip)) return &g_arp[i];
    return 0;
}
static void arp_add(const unsigned char* ip, const unsigned char* mac) {
    arp_entry_t* e = arp_find(ip);
    if (!e) {
        for (int i = 0; i < ARP_TABLE_SIZE; i++)
            if (!g_arp[i].valid) { e = &g_arp[i]; break; }
    }
    if (!e) e = &g_arp[0];   // вытеснить первый
    for (int i = 0; i < 4; i++) e->ip[i] = ip[i];
    for (int i = 0; i < 6; i++) e->mac[i] = mac[i];
    e->valid = 1;
}

// ============================================================
// Отправка Ethernet
// ============================================================
static int eth_send(const unsigned char* dst_mac, n_u16 ethertype,
                    const void* payload, unsigned int plen) {
    if (plen + 14 > sizeof(g_txbuf)) return -1;
    eth_hdr_t* h = (eth_hdr_t*)g_txbuf;
    for (int i = 0; i < 6; i++) h->dst[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) h->src[i] = g_mac[i];
    h->ethertype = be16(ethertype);
    const unsigned char* p = (const unsigned char*)payload;
    for (unsigned int i = 0; i < plen; i++) g_txbuf[14 + i] = p[i];
    // Ethernet minimum frame = 60 байт (без FCS). Короткие ARP-запросы
    // (14+28=42) QEMU может отбрасывать без ошибки. Паддим нулями.
    unsigned int total = 14 + plen;
    if (total < 60) {
        for (unsigned int i = total; i < 60; i++) g_txbuf[i] = 0;
        total = 60;
    }
    return virtio_net_send(g_txbuf, total);
}

// ============================================================
// ARP send/recv
// ============================================================
int net_arp_request(const unsigned char* target_ip) {
    arp_hdr_t a;
    a.htype = be16(1);
    a.ptype = be16(ETH_TYPE_IP);
    a.hlen = 6;
    a.plen = 4;
    a.op = be16(ARP_OP_REQUEST);
    for (int i = 0; i < 6; i++) a.sha[i] = g_mac[i];
    for (int i = 0; i < 4; i++) a.spa[i] = g_ip[i];
    for (int i = 0; i < 6; i++) a.tha[i] = 0;
    for (int i = 0; i < 4; i++) a.tpa[i] = target_ip[i];
    unsigned char bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    return eth_send(bcast, ETH_TYPE_ARP, &a, sizeof(a));
}

static void handle_arp(const unsigned char* p, unsigned int len) {
    if (len < sizeof(arp_hdr_t)) return;
    const arp_hdr_t* a = (const arp_hdr_t*)p;
    n_u16 op = be16(a->op);
    if (op == ARP_OP_REQUEST) {
        // Нас спрашивают?
        if (!ip_eq(a->tpa, g_ip)) return;
        // ARP reply.
        arp_hdr_t r;
        r.htype = be16(1);
        r.ptype = be16(ETH_TYPE_IP);
        r.hlen = 6; r.plen = 4;
        r.op = be16(ARP_OP_REPLY);
        for (int i = 0; i < 6; i++) r.sha[i] = g_mac[i];
        for (int i = 0; i < 4; i++) r.spa[i] = g_ip[i];
        for (int i = 0; i < 6; i++) r.tha[i] = a->sha[i];
        for (int i = 0; i < 4; i++) r.tpa[i] = a->spa[i];
        eth_send(a->sha, ETH_TYPE_ARP, &r, sizeof(r));
        serial_print("[net] ARP request from ");
        for (int i = 0; i < 4; i++) { serial_print_int(a->spa[i]); if (i<3) serial_print("."); }
        serial_print(" -> replied\n");
    } else if (op == ARP_OP_REPLY) {
        arp_add(a->spa, a->sha);
        serial_print("[net] ARP reply: ");
        for (int i = 0; i < 4; i++) { serial_print_int(a->spa[i]); if (i<3) serial_print("."); }
        serial_print(" is at ");
        for (int i = 0; i < 6; i++) { serial_print_hex(a->sha[i]); if (i<5) serial_print(":"); }
        serial_print("\n");
    }
}

// ============================================================
// ICMP echo reply
// ============================================================
static void handle_icmp(const unsigned char* ip_src,
                        const unsigned char* p, unsigned int len) {
    if (len < sizeof(icmp_hdr_t)) return;
    const icmp_hdr_t* ic = (const icmp_hdr_t*)p;
    if (ic->type == ICMP_ECHO_REQUEST) {
        // Отвечаем: type = REPLY, копируем остальное (id, seq, payload).
        unsigned char out[1500];
        if (len > sizeof(out)) return;
        for (unsigned int i = 0; i < len; i++) out[i] = p[i];
        icmp_hdr_t* o = (icmp_hdr_t*)out;
        o->type = ICMP_ECHO_REPLY;
        o->checksum = 0;
        o->checksum = ip_checksum(out, len);
        // Отправить IP-пакет обратно.
        extern int net_ip_send(const unsigned char* dst_ip, n_u8 proto,
                               const void* payload, unsigned int plen);
        net_ip_send(ip_src, IP_PROTO_ICMP, out, len);
        serial_print("[net] ICMP echo request -> reply\n");
    } else if (ic->type == ICMP_ECHO_REPLY) {
        serial_print("[net] ICMP echo reply: id=");
        serial_print_hex(be16(ic->id));
        serial_print(" seq=");
        serial_print_hex(be16(ic->seq));
        serial_print("\n");
    }
}

// ============================================================
// IPv4
// ============================================================
int net_ip_send(const unsigned char* dst_ip, n_u8 proto,
                const void* payload, unsigned int plen) {
    // Ищем MAC получателя (ARP) или gateway.
    const unsigned char* next_hop = dst_ip;
    // Простейшая логика: если IP не в нашей подсети — шлём на gateway.
    int in_subnet = 1;
    for (int i = 0; i < 4; i++)
        if ((dst_ip[i] & g_mask[i]) != (g_ip[i] & g_mask[i])) in_subnet = 0;
    if (!in_subnet) next_hop = g_gw;
    arp_entry_t* e = arp_find(next_hop);
    if (!e) {
        // ARP ещё не знаем — отправим запрос, а пакет потеряется (упрощение).
        net_arp_request(next_hop);
        return -1;
    }

    if (plen + 20 + 14 > sizeof(g_txbuf)) return -1;
    unsigned char pkt[2048];
    ip_hdr_t* ip = (ip_hdr_t*)pkt;
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = be16((n_u16)(20 + plen));
    ip->id = be16(0x0001);
    ip->frag_off = be16(0x4000);   // Don't Fragment
    ip->ttl = 64;
    ip->proto = proto;
    ip->checksum = 0;
    for (int i = 0; i < 4; i++) ip->src[i] = g_ip[i];
    for (int i = 0; i < 4; i++) ip->dst[i] = dst_ip[i];
    ip->checksum = ip_checksum(pkt, 20);
    const unsigned char* pp = (const unsigned char*)payload;
    for (unsigned int i = 0; i < plen; i++) pkt[20 + i] = pp[i];
    return eth_send(e->mac, ETH_TYPE_IP, pkt, 20 + plen);
}

static void handle_ip(const unsigned char* p, unsigned int len) {
    if (len < 20) return;
    const ip_hdr_t* ip = (const ip_hdr_t*)p;
    if ((ip->ver_ihl >> 4) != 4) return;
    unsigned int ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || len < ihl) return;
    unsigned int total = be16(ip->total_len);
    if (total > len) total = len;
    // Наш ли адрес?
    if (!ip_eq(ip->dst, g_ip) && !mac_broadcast(ip->dst)) {
        // Не нам. Игнор (без роутинга).
    }
    const unsigned char* payload = p + ihl;
    unsigned int plen = total - ihl;
    switch (ip->proto) {
    case IP_PROTO_ICMP: handle_icmp(ip->src, payload, plen); break;
    default: break;
    }
}

// ============================================================
// Приём
// ============================================================
static void eth_handle(const unsigned char* p, unsigned int len) {
    if (len < 14) return;
    const eth_hdr_t* h = (const eth_hdr_t*)p;
    // Принимаем только нам или broadcast.
    if (!mac_eq(h->dst, g_mac) && !mac_broadcast(h->dst)) return;
    n_u16 et = be16(h->ethertype);
    const unsigned char* payload = p + 14;
    unsigned int plen = len - 14;
    switch (et) {
    case ETH_TYPE_ARP: handle_arp(payload, plen); break;
    case ETH_TYPE_IP:  handle_ip(payload, plen);  break;
    default: break;
    }
}

int net_poll(void) {
    static int poll_count = 0;
    poll_count++;
    // Логируем первые 3 и каждые 10000 вызовов.
    if (poll_count <= 3 || (poll_count % 10000) == 0) {
        serial_print("[net_poll] #");
        serial_print_int(poll_count);
        serial_print("\n");
        virtio_net_debug();
        if (poll_count == 2) virtio_net_debug2();
    }
    int n = virtio_net_recv(g_rxbuf, sizeof(g_rxbuf));
    if (n <= 0) return 0;
    serial_print("[net] rx ");
    serial_print_int(n);
    serial_print(" bytes\n");
    eth_handle(g_rxbuf, (unsigned int)n);
    return 1;
}

// ============================================================
// Public
// ============================================================
void net_set_mac(const unsigned char* mac) {
    for (int i = 0; i < 6; i++) g_mac[i] = mac[i];
}
void net_set_ip(const unsigned char* ip, const unsigned char* gw, const unsigned char* mask) {
    for (int i = 0; i < 4; i++) g_ip[i] = ip[i];
    for (int i = 0; i < 4; i++) g_gw[i] = gw[i];
    for (int i = 0; i < 4; i++) g_mask[i] = mask[i];
}

void net_init(void) {
    const unsigned char* m = virtio_net_mac();
    if (m) net_set_mac(m);
    for (int i = 0; i < ARP_TABLE_SIZE; i++) g_arp[i].valid = 0;
    serial_print("[net] init, our MAC ");
    for (int i = 0; i < 6; i++) { serial_print_hex(g_mac[i]); if (i<5) serial_print(":"); }
    serial_print(", IP ");
    for (int i = 0; i < 4; i++) { serial_print_int(g_ip[i]); if (i<3) serial_print("."); }
    serial_print("\n");
    // Отправим ARP-запрос на gateway — QEMU ответит.
    net_arp_request(g_gw);
}

int net_ping(const unsigned char* ip) {
    unsigned char pkt[64];
    icmp_hdr_t* ic = (icmp_hdr_t*)pkt;
    ic->type = ICMP_ECHO_REQUEST;
    ic->code = 0;
    ic->checksum = 0;
    ic->id = be16(g_icmp_id);
    ic->seq = be16(g_icmp_seq++);
    for (int i = 0; i < 32; i++) pkt[8 + i] = (unsigned char)('A' + (i % 26));
    ic->checksum = ip_checksum(pkt, 8 + 32);
    return net_ip_send(ip, IP_PROTO_ICMP, pkt, 8 + 32);
}

const unsigned char* net_our_mac(void) { return g_mac; }
const unsigned char* net_our_ip(void)  { return g_ip; }

void net_dump(void) {
    serial_print("[net] MAC ");
    for (int i = 0; i < 6; i++) { serial_print_hex(g_mac[i]); if (i<5) serial_print(":"); }
    serial_print(", IP ");
    for (int i = 0; i < 4; i++) { serial_print_int(g_ip[i]); if (i<3) serial_print("."); }
    serial_print("\n");
}
