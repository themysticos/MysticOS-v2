// tcp.c - минимальный TCP-клиент для MysticOS 2.
#include "tcp.h"
#include "serial.h"
#include "net.h"

extern unsigned int timer_ticks(void);   // PIT 100 Гц = 10 мс/тик
extern void net_wait_tick(void);         // ожидание тика с sti (для syscall)

extern int net_ip_send(const unsigned char* dst_ip, n_u8 proto,
                       const void* payload, unsigned int plen);

static tcp_conn_t* g_conn = 0;

#define TCP_RXBUF 16384   // TLS: сертификат 2-3 КБ, нужен запас
static unsigned char g_rxbuf[TCP_RXBUF];
static unsigned int  g_rxlen = 0;

static n_u16 tcp_checksum(const unsigned char* ip_src,
                          const unsigned char* ip_dst,
                          const unsigned char* tcp, unsigned int tcp_len) {
    unsigned int sum = 0;
    sum += (ip_src[0] << 8) | ip_src[1];
    sum += (ip_src[2] << 8) | ip_src[3];
    sum += (ip_dst[0] << 8) | ip_dst[1];
    sum += (ip_dst[2] << 8) | ip_dst[3];
    sum += 6;
    sum += tcp_len;
    unsigned int i = 0;
    while (i + 1 < tcp_len) { sum += (tcp[i] << 8) | tcp[i + 1]; i += 2; }
    if (i < tcp_len) sum += tcp[i] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (n_u16)(~sum);
}

static n_u16 tcp_be16(n_u16 x) {
    return (n_u16)((x << 8) | (x >> 8));
}
#define be16(x) tcp_be16(x)

static n_u32 be32u(n_u32 x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8)
         | ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

static int tcp_send_segment(tcp_conn_t* c, n_u8 flags,
                            const void* data, unsigned int len) {
    unsigned char pkt[1500];
    tcp_hdr_t* t = (tcp_hdr_t*)pkt;
    t->src_port = be16(c->src_port);
    t->dst_port = be16(c->dst_port);
    t->seq      = be32u(c->send_seq);
    t->ack      = be32u(c->recv_ack);
    t->data_off = 0x50;
    t->flags    = flags;
    t->window   = be16(2048);
    t->checksum = 0;
    t->urgent   = 0;
    const unsigned char* p = (const unsigned char*)data;
    for (unsigned int i = 0; i < len; i++) pkt[20 + i] = p[i];
    unsigned int total = 20 + len;
    // tcp_checksum возвращает значение в BE-пространстве. При записи в
    // u16-поле на little-endian компилятор записывает low byte первым —
    // на проводе получается LE-порядок байт. Надо byteswap, чтобы на
    // проводе был корректный BE-порядок. Без этого QEMU молча дропает
    // пакет (TCP уходил в никуда, SYN-ACK не приходил).
    t->checksum = be16(tcp_checksum((const unsigned char*)net_our_ip(),
                                     c->dst_ip, pkt, total));
    serial_print("[tcp] seg flags="); serial_print_hex(flags);
    serial_print(" total="); serial_print_int((int)total);
    serial_print(" seq="); serial_print_hex(c->send_seq);
    serial_print(" csum="); serial_print_hex((unsigned int)t->checksum);
    serial_print("\n");
    return net_ip_send(c->dst_ip, 6, pkt, total);
}

static unsigned short g_next_src_port = 40000;

int net_tcp_connect(tcp_conn_t* c, const unsigned char* ip, unsigned short port) {
    // ВАЖНО: g_rxbuf глобальный (один на все соединения) и не очищается.
    // Без сброса остатки предыдущего ответа (напр. хвост HTTP от example.com)
    // попадали в следующий протокол (TLS читал 'e' из "HTTP" -> type=101).
    g_rxlen = 0;
    for (int i = 0; i < 4; i++) c->dst_ip[i] = ip[i];
    c->dst_port = port;
    c->src_port = g_next_src_port++;
    c->send_seq = 0x1000;
    c->recv_ack = 0;
    c->last_ack_from_peer = 0;
    c->state = TCP_SYN_SENT;
    g_conn = c;

    serial_print("[tcp] connect ");
    for (int i = 0; i < 4; i++) { serial_print_int(ip[i]); if (i<3) serial_print("."); }
    serial_print(":"); serial_print_int(port);
    serial_print(" src="); serial_print_int(c->src_port);
    serial_print("\n");

    // Отправляем SYN, ждём SYN-ACK. 3 попытки.
    for (int attempt = 0; attempt < 3; attempt++) {
        tcp_send_segment(c, TCP_SYN, 0, 0);
        // ВАЖНО: send_seq НЕ инкрементим здесь! Все SYNs (и retransmit)
        // идут с ОДНИМ seq (ISN). Иначе сервер видит 2 разных SYN и RST'ит.
        // Bump делается в tcp_handle_segment при получении SYN-ACK.
        serial_print("[tcp] SYN sent (attempt "); serial_print_int(attempt+1); serial_print(")\n");
        // Ожидание SYN-ACK ПО ВРЕМЕНИ (3 сек), а не по итерациям.
        { unsigned int t0 = timer_ticks();
          while ((timer_ticks() - t0) < 300) {
            net_wait_tick();
            if (c->state == TCP_ESTABLISHED) {
                serial_print("[tcp] ESTABLISHED\n");
                return 0;
            }
            if (c->state == TCP_CLOSED) {
                serial_print("[tcp] RST\n");
                return -1;
            }
          }
        }
    }
    serial_print("[tcp] connect timeout\n");
    return -1;
}

int net_tcp_send(tcp_conn_t* c, const void* data, unsigned int len) {
    if (c->state != TCP_ESTABLISHED) return -1;
    if (len > 1400) return -1;
    tcp_send_segment(c, TCP_ACK, data, len);   // без PSH — проверим, не в нём ли дело
    c->send_seq += len;
    serial_print("[tcp] sent "); serial_print_int((int)len); serial_print("\n");
    // Ожидание ACK ПО ВРЕМЕНИ (2 сек), а не по итерациям.
    { unsigned int t0 = timer_ticks();
      while ((timer_ticks() - t0) < 200) {
        net_wait_tick();
        if (c->last_ack_from_peer == c->send_seq) break;
        if (c->state != TCP_ESTABLISHED) break;
      }
    }
    return (int)len;
}

// НЕБЛОКИРУЮЩИЙ приём: забрать, что уже есть в g_rxbuf, без ожидания.
// Возвращает число байт (0 если пусто) или -1.
int net_tcp_recv_nb(tcp_conn_t* c, void* buf, unsigned int max) {
    (void)c;
    if (g_rxlen == 0) return 0;
    unsigned int n = g_rxlen;
    if (n > max) n = max;
    unsigned char* d = (unsigned char*)buf;
    for (unsigned int i = 0; i < n; i++) d[i] = g_rxbuf[i];
    for (unsigned int i = n; i < g_rxlen; i++) g_rxbuf[i - n] = g_rxbuf[i];
    g_rxlen -= n;
    return (int)n;
}

int net_tcp_recv(tcp_conn_t* c, void* buf, unsigned int max) {
    for (unsigned int i = 0; i < 3000000u; i++) {
        net_poll();
        if (g_rxlen > 0) break;
        if (c->state == TCP_CLOSED || c->state == TCP_TIME_WAIT) break;
    }
    if (g_rxlen == 0) return 0;
    unsigned int n = g_rxlen;
    if (n > max) n = max;
    unsigned char* d = (unsigned char*)buf;
    for (unsigned int i = 0; i < n; i++) d[i] = g_rxbuf[i];
    for (unsigned int i = n; i < g_rxlen; i++) g_rxbuf[i - n] = g_rxbuf[i];
    g_rxlen -= n;
    return (int)n;
}

void net_tcp_close(tcp_conn_t* c) {
    if (c->state != TCP_ESTABLISHED) return;
    serial_print("[tcp] FIN\n");
    tcp_send_segment(c, TCP_FIN | TCP_ACK, 0, 0);
    c->send_seq++;
    c->state = TCP_FIN_WAIT;
    for (unsigned int i = 0; i < 500000u; i++) {
        net_poll();
        if (c->state == TCP_TIME_WAIT || c->state == TCP_CLOSED) break;
    }
    c->state = TCP_CLOSED;
    if (g_conn == c) g_conn = 0;
}

void tcp_handle_segment(const unsigned char* ip_src,
                        const unsigned char* p, unsigned int len) {
    if (len < 20) return;
    const tcp_hdr_t* t = (const tcp_hdr_t*)p;
    unsigned short src_port = be16(t->src_port);
    unsigned short dst_port = be16(t->dst_port);

    if (!g_conn) return;
    if (dst_port != g_conn->src_port) return;
    if (src_port != g_conn->dst_port) return;
    for (int i = 0; i < 4; i++) if (ip_src[i] != g_conn->dst_ip[i]) return;

    unsigned int ihl = (t->data_off >> 4) * 4;
    if (ihl < 20 || ihl > len) return;
    unsigned int payload_len = len - ihl;
    const unsigned char* payload = p + ihl;

    n_u32 seq = be32u(t->seq);
    n_u32 ack = be32u(t->ack);
    n_u8  fl  = t->flags;

    serial_print("[tcp] rx fl="); serial_print_hex(fl);
    serial_print(" seq="); serial_print_hex(seq);
    serial_print(" ack="); serial_print_hex(ack);
    serial_print(" len="); serial_print_int((int)payload_len);
    serial_print("\n");

    if (fl & TCP_ACK) g_conn->last_ack_from_peer = ack;

    if (g_conn->state == TCP_SYN_SENT && (fl & TCP_SYN) && (fl & TCP_ACK)) {
        g_conn->recv_ack = seq + 1;
        g_conn->send_seq++;   // SYN съел один seq: 0x1000 -> 0x1001
        g_conn->state = TCP_ESTABLISHED;
        tcp_send_segment(g_conn, TCP_ACK, 0, 0);
        return;
    }

    if (g_conn->state == TCP_ESTABLISHED) {
        if (fl & TCP_RST) { g_conn->state = TCP_CLOSED; return; }
        if (payload_len > 0) {
            g_conn->recv_ack = seq + payload_len;
            unsigned int space = TCP_RXBUF - g_rxlen;
            unsigned int n = payload_len;
            if (n > space) n = space;
            for (unsigned int i = 0; i < n; i++) g_rxbuf[g_rxlen + i] = payload[i];
            g_rxlen += n;
            tcp_send_segment(g_conn, TCP_ACK, 0, 0);
        }
        if (fl & TCP_FIN) {
            g_conn->recv_ack++;
            tcp_send_segment(g_conn, TCP_ACK, 0, 0);
            g_conn->state = TCP_CLOSED;
        }
    }
}
