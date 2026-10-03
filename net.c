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

// ============================================================
// UDP: очередь из 4 слотов FIFO
// ============================================================
#define UDPQ_SIZE 4
#define UDPQ_MAX 1472   // типичный MTU 1500 - 20 IP - 8 UDP

typedef struct {
    unsigned char src_ip[4];
    unsigned short src_port;
    unsigned int len;
    unsigned char data[UDPQ_MAX];
} udpq_slot_t;

static udpq_slot_t g_udpq[UDPQ_SIZE];
static int g_udpq_head = 0;   // читаем отсюда
static int g_udpq_tail = 0;   // пишем сюда
static int g_udpq_count = 0;

static void udpq_push(const unsigned char* ip, unsigned short port,
                      const unsigned char* data, unsigned int len) {
    if (len > UDPQ_MAX) len = UDPQ_MAX;
    if (g_udpq_count >= UDPQ_SIZE) {
        // Очередь полная — выкидываем старый (head++).
        g_udpq_head = (g_udpq_head + 1) % UDPQ_SIZE;
        g_udpq_count--;
    }
    udpq_slot_t* s = &g_udpq[g_udpq_tail];
    for (int i = 0; i < 4; i++) s->src_ip[i] = ip[i];
    s->src_port = port;
    s->len = len;
    for (unsigned int i = 0; i < len; i++) s->data[i] = data[i];
    g_udpq_tail = (g_udpq_tail + 1) % UDPQ_SIZE;
    g_udpq_count++;
}

int net_udp_recv(unsigned char* src_ip, unsigned short* src_port,
                 void* buf, unsigned int max) {
    if (g_udpq_count <= 0) return -1;
    udpq_slot_t* s = &g_udpq[g_udpq_head];
    if (src_ip) for (int i = 0; i < 4; i++) src_ip[i] = s->src_ip[i];
    if (src_port) *src_port = s->src_port;
    unsigned int n = s->len;
    if (n > max) n = max;
    unsigned char* d = (unsigned char*)buf;
    for (unsigned int i = 0; i < n; i++) d[i] = s->data[i];
    g_udpq_head = (g_udpq_head + 1) % UDPQ_SIZE;
    g_udpq_count--;
    return (int)n;
}

int net_udp_send(const unsigned char* dst_ip, unsigned short src_port,
                 unsigned short dst_port, const void* data, unsigned int len) {
    if (len > UDPQ_MAX) return -1;
    unsigned char pkt[1600];
    udp_hdr_t* u = (udp_hdr_t*)pkt;
    u->src_port = be16(src_port);
    u->dst_port = be16(dst_port);
    u->length   = be16((n_u16)(8 + len));
    u->checksum = 0;   // IPv4 разрешает 0 = не считать
    const unsigned char* p = (const unsigned char*)data;
    for (unsigned int i = 0; i < len; i++) pkt[8 + i] = p[i];
    return net_ip_send(dst_ip, IP_PROTO_UDP, pkt, 8 + len);
}

static void handle_dhcp(const unsigned char* p, unsigned int len);
static void handle_dns(const unsigned char* p, unsigned int len);

static void handle_udp(const unsigned char* ip_src,
                       const unsigned char* p, unsigned int len) {
    if (len < 8) return;
    const udp_hdr_t* u = (const udp_hdr_t*)p;
    unsigned short dst_port = be16(u->dst_port);
    unsigned int ulen = be16(u->length);
    if (ulen < 8 || ulen > len) ulen = len;
    unsigned int payload_len = ulen - 8;
    const unsigned char* payload = p + 8;
    // DHCP: dst_port == 68 (клиент).
    if (dst_port == 68) {
        handle_dhcp(payload, payload_len);
        return;
    }
    // DNS: dst_port == 53000 (наш клиентский порт).
    if (dst_port == 53000) {
        handle_dns(payload, payload_len);
        return;
    }
    (void)dst_port;
    unsigned short sport = be16(u->src_port);
    udpq_push(ip_src, sport, payload, payload_len);
    serial_print("[net] UDP ");
    serial_print_int((int)sport);
    serial_print(" -> ");
    serial_print_int((int)be16(u->dst_port));
    serial_print(" ");
    serial_print_int((int)payload_len);
    serial_print(" bytes\n");
}

// ============================================================
// DHCP (RFC 2131). Упрощённый клиент.
// ============================================================
typedef struct {
    n_u8  op, htype, hlen, hops;
    n_u32 xid;
    n_u16 secs, flags;
    n_u8  ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    n_u8  chaddr[16];
    n_u8  sname[64];
    n_u8  file[128];
    n_u8  magic[4];
    n_u8  options[308];
} __attribute__((packed)) dhcp_msg_t;

static n_u32 g_dhcp_xid = 0x12345678;
static int  g_dhcp_done = 0;
static int  g_dhcp_state = 0;
#define DHCP_OP_REQUEST   1
#define DHCP_OP_REPLY     2
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5
#define DHCP_OPT_MSG_TYPE 53
#define DHCP_OPT_SUBNET   1
#define DHCP_OPT_ROUTER   3
#define DHCP_OPT_DNS      6
#define DHCP_OPT_REQIP    50
#define DHCP_OPT_SERVERID 54
#define DHCP_OPT_PARAM    55
#define DHCP_OPT_END      255

static int g_dhcp_offer_ip[4]  = {0,0,0,0};
static int g_dhcp_server[4]    = {0,0,0,0};

int net_dhcp_done(void) { return g_dhcp_done; }

int net_dhcp_discover(void) {
    unsigned char pkt[600];
    for (unsigned int i = 0; i < sizeof(pkt); i++) pkt[i] = 0;
    dhcp_msg_t* m = (dhcp_msg_t*)pkt;
    m->op = DHCP_OP_REQUEST;
    m->htype = 1; m->hlen = 6; m->hops = 0;
    m->xid = be32(g_dhcp_xid);
    m->secs = be16(0);
    m->flags = be16(0x8000);
    for (int i = 0; i < 6; i++) m->chaddr[i] = g_mac[i];
    m->magic[0] = 63; m->magic[1] = 82; m->magic[2] = 53; m->magic[3] = 63;
    int o = 0;
    m->options[o++] = DHCP_OPT_MSG_TYPE; m->options[o++] = 1;
    m->options[o++] = DHCP_MSG_DISCOVER;
    m->options[o++] = DHCP_OPT_PARAM; m->options[o++] = 3;
    m->options[o++] = DHCP_OPT_SUBNET; m->options[o++] = DHCP_OPT_ROUTER;
    m->options[o++] = DHCP_OPT_DNS;
    m->options[o++] = DHCP_OPT_END;
    unsigned int hdr = 240 + 4 + o;
    if (hdr < 300) hdr = 300;
    unsigned char bcast[4] = {255,255,255,255};
    serial_print("[dhcp] sending DISCOVER\n");
    g_dhcp_state = 1;
    return net_udp_send(bcast, 68, 67, pkt, hdr);
}

static void handle_dhcp(const unsigned char* p, unsigned int len) {
    if (len < 240) return;
    const dhcp_msg_t* m = (const dhcp_msg_t*)p;
    if (m->op != DHCP_OP_REPLY) return;
    int msg_type = 0;
    const unsigned char* opt = m->options;
    unsigned int opt_len = len - 240;
    unsigned int i = 0;
    while (i < opt_len) {
        n_u8 code = opt[i++];
        if (code == 0) continue;
        if (code == 255) break;
        if (i >= opt_len) break;
        n_u8 olen = opt[i++];
        if (i + olen > opt_len) break;
        if (code == DHCP_OPT_MSG_TYPE && olen == 1) msg_type = opt[i];
        else if (code == DHCP_OPT_SERVERID && olen == 4)
            for (int k = 0; k < 4; k++) g_dhcp_server[k] = opt[i+k];
        else if (code == DHCP_OPT_ROUTER && olen >= 4)
            for (int k = 0; k < 4; k++) g_gw[k] = opt[i+k];
        else if (code == DHCP_OPT_SUBNET && olen == 4)
            for (int k = 0; k < 4; k++) g_mask[k] = opt[i+k];
        i += olen;
    }
    serial_print("[dhcp] reply msg_type=");
    serial_print_int(msg_type);
    serial_print(" yiaddr=");
    for (int k = 0; k < 4; k++) { serial_print_int(m->yiaddr[k]); if (k<3) serial_print("."); }
    serial_print("\n");

    if (msg_type == DHCP_MSG_OFFER && g_dhcp_state == 1) {
        for (int k = 0; k < 4; k++) g_dhcp_offer_ip[k] = m->yiaddr[k];
        unsigned char pkt[600];
        for (unsigned int j = 0; j < sizeof(pkt); j++) pkt[j] = 0;
        dhcp_msg_t* r = (dhcp_msg_t*)pkt;
        r->op = DHCP_OP_REQUEST;
        r->htype = 1; r->hlen = 6; r->hops = 0;
        r->xid = be32(g_dhcp_xid);
        r->flags = be16(0x8000);
        for (int k = 0; k < 6; k++) r->chaddr[k] = g_mac[k];
        r->magic[0] = 63; r->magic[1] = 82; r->magic[2] = 53; r->magic[3] = 63;
        int o = 0;
        r->options[o++] = DHCP_OPT_MSG_TYPE; r->options[o++] = 1;
        r->options[o++] = DHCP_MSG_REQUEST;
        r->options[o++] = DHCP_OPT_REQIP; r->options[o++] = 4;
        for (int k = 0; k < 4; k++) r->options[o++] = m->yiaddr[k];
        r->options[o++] = DHCP_OPT_SERVERID; r->options[o++] = 4;
        for (int k = 0; k < 4; k++) r->options[o++] = g_dhcp_server[k];
        r->options[o++] = DHCP_OPT_END;
        unsigned int hdr = 240 + 4 + o;
        if (hdr < 300) hdr = 300;
        unsigned char bcast[4] = {255,255,255,255};
        serial_print("[dhcp] sending REQUEST\n");
        g_dhcp_state = 2;
        net_udp_send(bcast, 68, 67, pkt, hdr);
    } else if (msg_type == DHCP_MSG_ACK) {
        // QEMU user-mode может ответить ACK сразу на DISCOVER (без OFFER).
        // Принимаем ACK в любом состоянии.
        for (int k = 0; k < 4; k++) g_ip[k] = m->yiaddr[k];
        g_dhcp_done = 1;
        g_dhcp_state = 3;
        serial_print("[dhcp] ACK. our IP ");
        for (int k = 0; k < 4; k++) { serial_print_int(g_ip[k]); if (k<3) serial_print("."); }
        serial_print(" gw ");
        for (int k = 0; k < 4; k++) { serial_print_int(g_gw[k]); if (k<3) serial_print("."); }
        serial_print(" mask ");
        for (int k = 0; k < 4; k++) { serial_print_int(g_mask[k]); if (k<3) serial_print("."); }
        serial_print("\n");
    }
}

// ============================================================
// DNS (RFC 1035). Упрощённый клиент.
// ============================================================
static unsigned char g_dns_server[4] = {10, 0, 2, 3};
static unsigned short g_dns_tx_id = 0x1234;
static unsigned char g_dns_result_ip[4];
static int g_dns_got_reply = 0;

static int build_qname(const char* name, unsigned char* out, int max) {
    int o = 0;
    const char* p = name;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int label_len = (int)(dot - p);
        if (label_len == 0 || label_len > 63) return -1;
        if (o + 1 + label_len >= max) return -1;
        out[o++] = (unsigned char)label_len;
        for (int i = 0; i < label_len; i++) out[o++] = (unsigned char)p[i];
        p = dot;
        if (*p == '.') p++;
    }
    if (o >= max) return -1;
    out[o++] = 0;
    return o;
}

int net_dns_resolve(const char* name, unsigned char* out_ip) {
    unsigned char query[256];
    for (unsigned int i = 0; i < sizeof(query); i++) query[i] = 0;
    query[0] = (g_dns_tx_id >> 8) & 0xFF;
    query[1] = g_dns_tx_id & 0xFF;
    query[2] = 0x01; query[3] = 0x00;
    query[4] = 0; query[5] = 1;
    query[6] = 0; query[7] = 0;
    query[8] = 0; query[9] = 0;
    query[10]= 0; query[11]= 0;
    int qn = build_qname(name, query + 12, 200);
    if (qn < 0) return -1;
    int off = 12 + qn;
    query[off++] = 0x00; query[off++] = 0x01;
    query[off++] = 0x00; query[off++] = 0x01;

    serial_print("[dns] query "); serial_print(name); serial_print("\n");
    g_dns_got_reply = 0;
    // Шаг 1: разрешить ARP для DNS-сервера (10.0.2.3).
    // Иначе net_udp_send вернёт -1.
    net_arp_request(g_dns_server);
    for (int i = 0; i < 50000; i++) net_poll();
    // Шаг 2: отправить один раз. net_udp_send возвращает число байт
    // при успехе (положительное!) или -1 при ошибке.
    int sent = net_udp_send(g_dns_server, 53000, 53, query, (unsigned int)off);
    if (sent < 0) {
        serial_print("[dns] send failed\n");
        return -1;
    }
    for (unsigned int spin = 0; spin < 2000000u; spin++) {
        net_poll();
        if (g_dns_got_reply) {
            for (int i = 0; i < 4; i++) out_ip[i] = g_dns_result_ip[i];
            serial_print("[dns] resolved "); serial_print(name); serial_print(" -> ");
            for (int i = 0; i < 4; i++) { serial_print_int(out_ip[i]); if (i<3) serial_print("."); }
            serial_print("\n");
            return 0;
        }
    }
    serial_print("[dns] timeout\n");
    return -1;
}

static void handle_dns(const unsigned char* p, unsigned int len) {
    serial_print("[dns] rx "); serial_print_int((int)len); serial_print(" bytes\n");
    if (len < 12) return;
    unsigned short id = (p[0] << 8) | p[1];
    serial_print("[dns] id="); serial_print_hex(id); serial_print(" want="); serial_print_hex(g_dns_tx_id); serial_print("\n");
    if (id != g_dns_tx_id) return;
    unsigned short flags = (p[2] << 8) | p[3];
    if ((flags & 0x8000) == 0) return;
    unsigned short qd = (p[4] << 8) | p[5];
    unsigned short an = (p[6] << 8) | p[7];
    serial_print("[dns] qd="); serial_print_int(qd); serial_print(" an="); serial_print_int(an); serial_print("\n");
    if (an == 0) return;
    unsigned int off = 12;
    for (int q = 0; q < qd; q++) {
        while (off < len && p[off] != 0) {
            if ((p[off] & 0xC0) == 0xC0) { off += 2; goto q_done; }
            off += 1 + p[off];
        }
        off++;
q_done:
        off += 4;
    }
    for (int a = 0; a < an && off < len; a++) {
        if ((p[off] & 0xC0) == 0xC0) { off += 2; }
        else { while (off < len && p[off]) off += 1 + p[off]; off++; }
        if (off + 10 > len) break;
        unsigned short type = (p[off] << 8) | p[off+1]; off += 2;
        unsigned short cls  = (p[off] << 8) | p[off+1]; off += 2;
        off += 4;
        unsigned short rdlen = (p[off] << 8) | p[off+1]; off += 2;
        if (type == 1 && cls == 1 && rdlen == 4 && off + 4 <= len) {
            for (int i = 0; i < 4; i++) g_dns_result_ip[i] = p[off + i];
            g_dns_got_reply = 1;
            return;
        }
        off += rdlen;
    }
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
    case IP_PROTO_UDP:  handle_udp(ip->src, payload, plen);  break;
    case 6: {   // TCP
        extern void tcp_handle_segment(const unsigned char* ip_src,
                                       const unsigned char* p, unsigned int len);
        tcp_handle_segment(ip->src, payload, plen);
        break;
    }
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
    // Отладка: спам убран. Включай по мере надобности.
    if (0) {
        virtio_net_debug();
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
