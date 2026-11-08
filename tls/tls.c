// tls/tls.c - TLS 1.2 client (RSA + AES-128-CBC + SHA-256 + HMAC). Часть 1.
// Структура сессии, PRF, record layer.
#include "tls.h"
#include "sha256.h"
#include "hmac.h"
#include "aes.h"
#include "rsa.h"
#include "x509.h"
#include "bigint.h"
#include "rand.h"
#include "ec.h"
#include "gcm.h"
#include "../tcp.h"
#include "../serial.h"

#define TLS_VER_MAJOR 0x03
#define TLS_VER_MINOR 0x03   // TLS 1.2

// Record types
#define RT_CHANGE_CIPHER_SPEC 20
#define RT_ALERT              21
#define RT_HANDSHAKE          22
#define RT_APPLICATION_DATA   23

// Handshake types
#define HS_CLIENT_HELLO        1
#define HS_SERVER_HELLO        2
#define HS_CERTIFICATE        11
#define HS_SERVER_KEY_EXCHANGE 12
#define HS_SERVER_HELLO_DONE  14
#define HS_CLIENT_KEY_EXCHANGE 16
#define HS_FINISHED           20

struct tls_session {
    tcp_conn_t conn;
    int ready;
    unsigned char client_write_key[16];
    unsigned char server_write_key[16];
    unsigned char client_write_mac[32];
    unsigned char server_write_mac[32];
    unsigned char client_iv[16];
    unsigned char server_iv[16];
    unsigned long long client_seq;
    unsigned long long server_seq;
    unsigned int mac_len;      // 32 для SHA256 (0xC027/0x003C), 20 для SHA1 (0xC013)
    int use_sha1;              // 1 = MAC через HMAC-SHA1, 0 = HMAC-SHA256
    int nonblock;              // 1 = recv_record не ждёт (возвращает 0 если нет данных)
    int use_gcm;               // 1 = AES-128-GCM (0xC02F), 0 = CBC
    unsigned char client_write_iv_gcm[4];   // salt для GCM (4 байта)
    unsigned char server_write_iv_gcm[4];
    unsigned char app_buf[TLS_MAX_RECORD];
    unsigned int app_len;
    unsigned int app_pos;
};

// ---- PRF (RFC 5246, раздел 5): P_SHA256(secret, label||seed) ----
static void prf_sha256(const unsigned char* secret, unsigned int slen,
                       const char* label,
                       const unsigned char* seed, unsigned int seedlen,
                       unsigned char* out, unsigned int outlen) {
    unsigned int llen = 0; while (label[llen]) llen++;
    unsigned char seed_full[256];
    for (unsigned int i = 0; i < llen; i++) seed_full[i] = (unsigned char)label[i];
    for (unsigned int i = 0; i < seedlen && llen + i < 256; i++) seed_full[llen + i] = seed[i];
    unsigned int sf_len = llen + seedlen;

    unsigned char A[32];
    hmac_sha256(secret, slen, seed_full, sf_len, A);
    unsigned int pos = 0;
    while (pos < outlen) {
        unsigned char buf[32 + 256];
        for (int i = 0; i < 32; i++) buf[i] = A[i];
        for (unsigned int i = 0; i < sf_len; i++) buf[32 + i] = seed_full[i];
        unsigned char chunk[32];
        hmac_sha256(secret, slen, buf, 32 + sf_len, chunk);
        unsigned int take = (outlen - pos < 32) ? (outlen - pos) : 32;
        for (unsigned int i = 0; i < take; i++) out[pos + i] = chunk[i];
        pos += take;
        hmac_sha256(secret, slen, A, 32, A);
    }
}

// ---- Record layer ----
static int send_record(tls_session* s, unsigned char type,
                       const unsigned char* payload, unsigned int len) {
    // Собираем hdr+payload в ОДИН буфер и шлём одним TCP-сегментом.
    // Раньше шли два сегмента (5+67) — сервер получал по частям и мог
    // не собрать. Один сегмент — надёжнее.
    unsigned char buf[5 + 16384];
    buf[0] = type;
    buf[1] = TLS_VER_MAJOR; buf[2] = TLS_VER_MINOR;
    buf[3] = (len >> 8) & 0xFF; buf[4] = len & 0xFF;
    for (unsigned int i = 0; i < len; i++) buf[5 + i] = payload[i];
    return net_tcp_send(&s->conn, buf, 5 + len);
}

static int recv_record(tls_session* s, unsigned char* type,
                       unsigned char* buf, unsigned int max) {
    extern unsigned int timer_ticks(void);
    extern int net_poll(void);
    extern void net_wait_tick(void);
    unsigned char hdr[5];
    unsigned int got = 0, t0 = timer_ticks();
    while (got < 5) {
        // Неблокирующий режим (для HTTP-браузера): один poll, нет данных -> 0.
        if (s->nonblock) {
            net_poll();
            int nb = net_tcp_recv_nb(&s->conn, hdr + got, 5 - got);
            if (nb > 0) { got += nb; continue; }
            if (nb < 0) return -1;
            return 0;   // ещё нет полного заголовка — пусть вызывающий повторит
        }
        // net_wait_tick: poll + ожидание тика со sti. Нужно из syscall (IF=0),
        // иначе timer_ticks не растёт и цикл по времени не завершится.
        net_wait_tick();
        int n = net_tcp_recv_nb(&s->conn, hdr + got, 5 - got);
        if (n > 0) { got += n; continue; }
        if (n < 0) return -1;
        if ((timer_ticks() - t0) > 1000) return -1;   // 10 сек
    }
    unsigned int len = ((unsigned int)hdr[3] << 8) | hdr[4];
    if (len > max) return -1;
    *type = hdr[0];
    serial_print("[tls] rec hdr type="); serial_print_int(*type);
    serial_print(" len="); serial_print_int((int)len); serial_print("\n");
    // Alert (type 21) — 2 байта: level, description. Сразу читаем и печатаем.
    if (*type == RT_ALERT) {
        unsigned int g2 = 0, t2 = timer_ticks();
        while (g2 < len && g2 < 2) {
            net_poll();
            int n2 = net_tcp_recv_nb(&s->conn, buf + g2, 2 - g2);
            if (n2 > 0) { g2 += n2; continue; }
            if ((timer_ticks() - t2) > 200) break;
        }
        if (g2 >= 2) {
            serial_print("[tls] ALERT level="); serial_print_int(buf[0]);
            serial_print(" desc="); serial_print_int(buf[1]); serial_print("\n");
        }
        return -1;
    }
    got = 0; t0 = timer_ticks();
    while (got < len) {
        if (s->nonblock) {
            net_poll();
            int nb = net_tcp_recv_nb(&s->conn, buf + got, len - got);
            if (nb > 0) { got += nb; continue; }
            if (nb < 0) return -1;
            return 0;   // запись ещё не целиком пришла
        }
        net_wait_tick();
        int n = net_tcp_recv_nb(&s->conn, buf + got, len - got);
        if (n > 0) { got += n; continue; }
        if (n < 0) return -1;
        if ((timer_ticks() - t0) > 1000) return -1;
    }
    return (int)len;
}

// ---- Шифрование/дешифрование записи (AES-128-CBC + HMAC-SHA256) ----
static void record_mac(const unsigned char* mac_key, unsigned int key_len,
                       int use_sha1,
                       unsigned long long seq, unsigned char type,
                       const unsigned char* data, unsigned int len,
                       unsigned char out_mac[32]) {
    unsigned char hdr[13];
    for (int i = 0; i < 8; i++) hdr[i] = (unsigned char)(seq >> (56 - i*8));
    hdr[8] = type; hdr[9] = TLS_VER_MAJOR; hdr[10] = TLS_VER_MINOR;
    hdr[11] = (len >> 8) & 0xFF; hdr[12] = len & 0xFF;
    unsigned char buf[13 + TLS_MAX_RECORD];
    for (int i = 0; i < 13; i++) buf[i] = hdr[i];
    for (unsigned int i = 0; i < len; i++) buf[13 + i] = data[i];
    if (use_sha1)
        hmac_sha1(mac_key, key_len, buf, 13 + len, out_mac);   // 20 байт
    else
        hmac_sha256(mac_key, key_len, buf, 13 + len, out_mac); // 32 байта
}

static int send_encrypted(tls_session* s, unsigned char type,
                          const unsigned char* plain, unsigned int len) {
    if (s->use_gcm) {
        // RFC 5288: explicit_nonce(8)||ciphertext||tag(16). nonce=salt(4)||expl(8).
        // AAD = seq(8)||type(1)||ver(2)||plaintext_len(2). explicit_nonce = seq.
        static unsigned char rec[TLS_MAX_RECORD + 32];
        unsigned char nonce[12], aad[13], tag[16];
        for (int i = 0; i < 4; i++) nonce[i] = s->client_write_iv_gcm[i];
        unsigned long long seq = s->client_seq;
        for (int i = 0; i < 8; i++) rec[i] = (unsigned char)(seq >> (56 - i*8));
        for (int i = 0; i < 8; i++) nonce[4 + i] = rec[i];
        for (int i = 0; i < 8; i++) aad[i] = rec[i];
        aad[8] = type; aad[9] = TLS_VER_MAJOR; aad[10] = TLS_VER_MINOR;
        aad[11] = (len >> 8) & 0xFF; aad[12] = len & 0xFF;
        gcm_ctx g; gcm_init(&g, s->client_write_key);
        gcm_encrypt(&g, nonce, aad, 13, plain, len, rec + 8, tag);
        s->client_seq++;
        for (int i = 0; i < 16; i++) rec[8 + len + i] = tag[i];
        return send_record(s, type, rec, 8 + len + 16);
    }
    unsigned int ml = s->mac_len;
    unsigned char mac[32];
    record_mac(s->client_write_mac, ml, s->use_sha1, s->client_seq, type, plain, len, mac);
    s->client_seq++;
    unsigned int frag_len = len + ml;
    unsigned int pad_len = 16 - 1 - (frag_len % 16);
    static unsigned char frag[13 + TLS_MAX_RECORD + 16];
    for (unsigned int i = 0; i < len; i++) frag[i] = plain[i];
    for (unsigned int i = 0; i < ml; i++) frag[len + i] = mac[i];
    unsigned int pos = len + ml;
    for (unsigned int i = 0; i <= pad_len; i++) frag[pos++] = (unsigned char)pad_len;
    static unsigned char enc[TLS_MAX_RECORD + 32];
    aes128_ctx aes;
    aes128_init(&aes, s->client_write_key);
    // TLS 1.2 (0x0303): CBC использует EXPLICIT IV — случайные 16 байт в
    // начале записи (а не чейнинг от предыдущего блока, как в TLS 1.0).
    // Иначе сервер не расшифрует и пришлёт bad_record_mac (alert desc=20).
    unsigned char exiv[16];
    rand_bytes(exiv, 16);
    for (int i = 0; i < 16; i++) enc[i] = exiv[i];
    aes128_cbc_encrypt(&aes, exiv, frag, enc + 16, pos);
    return send_record(s, type, enc, 16 + pos);
}

static int recv_encrypted(tls_session* s, unsigned char* type,
                          unsigned char* plain, unsigned int max) {
    if (s->use_gcm) {
        static unsigned char enc[TLS_MAX_RECORD + 32];
        int n = recv_record(s, type, enc, sizeof(enc));
        if (n == 0) return 0;             // нет данных (nonblock) — не ошибка
        if (n < 8 + 16) return -1;
        unsigned int ctlen = (unsigned int)n - 8 - 16;
        if (ctlen > max) return -1;
        unsigned char nonce[12], aad[13];
        for (int i = 0; i < 4; i++) nonce[i] = s->server_write_iv_gcm[i];
        for (int i = 0; i < 8; i++) nonce[4 + i] = enc[i];
        unsigned long long seq = s->server_seq;
        for (int i = 0; i < 8; i++) aad[i] = (unsigned char)(seq >> (56 - i*8));
        aad[8] = *type; aad[9] = TLS_VER_MAJOR; aad[10] = TLS_VER_MINOR;
        aad[11] = (ctlen >> 8) & 0xFF; aad[12] = ctlen & 0xFF;
        gcm_ctx g; gcm_init(&g, s->server_write_key);
        if (gcm_decrypt(&g, nonce, aad, 13, enc + 8, ctlen, plain, enc + 8 + ctlen) != 0)
            return -1;
        s->server_seq++;
        return (int)ctlen;
    }
    static unsigned char enc[TLS_MAX_RECORD + 16];
    int n = recv_record(s, type, enc, sizeof(enc));
    if (n == 0) return 0;                 // нет данных (nonblock) — не ошибка
    if (n < 0) return -1;
    if ((n % 16) != 0) return -1;
    if (n < 32) return -1;   // минимум: explicit IV(16) + блок
    static unsigned char dec[TLS_MAX_RECORD + 16];
    aes128_ctx aes;
    aes128_init(&aes, s->server_write_key);
    // TLS 1.2 CBC: explicit IV — первые 16 байт записи. Расшифровываем
    // остаток от них; после расшифровки длина уменьшается на 16.
    unsigned char exiv[16];
    for (int i = 0; i < 16; i++) exiv[i] = enc[i];
    unsigned int ct_len = (unsigned int)n - 16;
    aes128_cbc_decrypt(&aes, exiv, enc + 16, dec, ct_len);
    n = (int)ct_len;
    unsigned char pad = dec[n - 1];
    if ((int)pad + 1 > n) return -1;
    int content_and_mac = n - pad - 1;
    unsigned int ml = s->mac_len;
    if (content_and_mac < (int)ml) return -1;
    unsigned int content_len = content_and_mac - ml;
    if (content_len > max) return -1;
    for (unsigned int i = 0; i < content_len; i++) plain[i] = dec[i];
    unsigned char expect[32];
    record_mac(s->server_write_mac, ml, s->use_sha1, s->server_seq, *type, plain, content_len, expect);
    s->server_seq++;
    int ok = 1;
    for (unsigned int i = 0; i < ml; i++) if (dec[content_len + i] != expect[i]) { ok = 0; break; }
    if (!ok) return -1;
    return (int)content_len;
}

// ---- Handshake ----
static unsigned char g_hs_buf[16384];
static unsigned int  g_hs_len;

static void hs_append(const unsigned char* data, unsigned int len) {
    if (g_hs_len + len > sizeof(g_hs_buf)) return;
    for (unsigned int i = 0; i < len; i++) g_hs_buf[g_hs_len + i] = data[i];
    g_hs_len += len;
}

static int send_handshake(tls_session* s, unsigned char hs_type,
                          const unsigned char* body, unsigned int body_len) {
    static unsigned char msg[4 + 2048];
    msg[0] = hs_type;
    msg[1] = (body_len >> 16) & 0xFF;
    msg[2] = (body_len >> 8) & 0xFF;
    msg[3] = body_len & 0xFF;
    for (unsigned int i = 0; i < body_len; i++) msg[4 + i] = body[i];
    hs_append(msg, 4 + body_len);
    return send_record(s, RT_HANDSHAKE, msg, 4 + body_len);
}

static int recv_handshake(tls_session* s, unsigned char* hs_type,
                          unsigned char* body, unsigned int max) {
    unsigned char type;
    static unsigned char buf[16384];
    int n = recv_record(s, &type, buf, sizeof(buf));
    if (n < 0) return -1;
    if (type != RT_HANDSHAKE) {
        serial_print("[tls] expected handshake, got type=");
        serial_print_int(type); serial_print("\n");
        return -1;
    }
    if (n < 4) return -1;
    *hs_type = buf[0];
    unsigned int blen = ((unsigned int)buf[1] << 16) | ((unsigned int)buf[2] << 8) | buf[3];
    if (blen > max || 4 + blen > (unsigned int)n) return -1;
    for (unsigned int i = 0; i < blen; i++) body[i] = buf[4 + i];
    hs_append(buf, 4 + blen);
    return (int)blen;
}

static int build_client_hello(unsigned char* out, const char* sni,
                              const unsigned char* client_random) {
    unsigned int p = 0;
    out[p++] = TLS_VER_MAJOR; out[p++] = TLS_VER_MINOR;
    for (int i = 0; i < 32; i++) out[p++] = client_random[i];
    out[p++] = 0;
    // Cipher suites: приоритет ECDHE_RSA_AES128_CBC_SHA256 (0xC027),
    // затем RSA_AES128_CBC_SHA256 (0x003C) как fallback.
    out[p++] = 0x00; out[p++] = 0x08;   // длина списка = 8 байт (4 suite)
    out[p++] = 0xC0; out[p++] = 0x2F;   // ECDHE_RSA_AES128_GCM_SHA256 (AEAD, приоритет)
    out[p++] = 0xC0; out[p++] = 0x13;   // ECDHE_RSA_AES128_CBC_SHA
    out[p++] = 0xC0; out[p++] = 0x27;   // ECDHE_RSA_AES128_CBC_SHA256
    out[p++] = 0x00; out[p++] = 0x3C;   // RSA_AES128_CBC_SHA256 (fallback)
    out[p++] = 0x01; out[p++] = 0x00;
    unsigned int ext_len_pos = p; p += 2;
    unsigned int ext_start = p;
    {
        out[p++] = 0x00; out[p++] = 0x00;
        unsigned int slen = 0; while (sni[slen]) slen++;
        unsigned int list_len = 1 + 2 + slen;
        // ВАЖНО: ext data length = list_len + 2 (ServerNameList включает
        // 2-байтовое поле длины + сами данные). Раньше было list_len
        // (на 2 байта меньше) -> сервер видел обрезанный SNI -> Alert.
        unsigned int ext_data_len = 2 + list_len;
        out[p++] = (ext_data_len >> 8) & 0xFF; out[p++] = ext_data_len & 0xFF;
        out[p++] = (list_len >> 8) & 0xFF; out[p++] = list_len & 0xFF;
        out[p++] = 0;
        out[p++] = (slen >> 8) & 0xFF; out[p++] = slen & 0xFF;
        for (unsigned int i = 0; i < slen; i++) out[p++] = (unsigned char)sni[i];
    }
    {
        // supported_groups (ext 10): secp256r1 (0x0017)
        out[p++] = 0x00; out[p++] = 0x0A;
        out[p++] = 0x00; out[p++] = 0x04;
        out[p++] = 0x00; out[p++] = 0x02;
        out[p++] = 0x00; out[p++] = 0x17;
    }
    {
        // ec_point_formats (ext 11): uncompressed (0)
        out[p++] = 0x00; out[p++] = 0x0B;
        out[p++] = 0x00; out[p++] = 0x02;
        out[p++] = 0x01; out[p++] = 0x00;
    }
    {
        // signature_algorithms (ext 13): rsa_pkcs1_sha256 (0x0401)
        out[p++] = 0x00; out[p++] = 0x0D;
        out[p++] = 0x00; out[p++] = 0x04;
        out[p++] = 0x00; out[p++] = 0x02;
        out[p++] = 0x04; out[p++] = 0x01;
    }
    unsigned int ext_total = p - ext_start;
    out[ext_len_pos] = (ext_total >> 8) & 0xFF;
    out[ext_len_pos + 1] = ext_total & 0xFF;
    return (int)p;
}

tls_session* tls_connect(const unsigned char* ip, unsigned short port, const char* sni_host) {
    static tls_session s;
    for (unsigned int i = 0; i < sizeof(s); i++) ((unsigned char*)&s)[i] = 0;
    s.mac_len = 32; s.use_sha1 = 0;   // дефолт SHA-256; переопределим по suite
    if (net_tcp_connect(&s.conn, ip, port) != 0) return 0;
    g_hs_len = 0;
    unsigned char client_random[32];
    rand_bytes(client_random, 32);
    unsigned char hello[512];
    int hlen = build_client_hello(hello, sni_host, client_random);
    if (hlen < 0) return 0;
    serial_print("[tls] sending ClientHello ("); serial_print_int(hlen); serial_print(")\n");
    // net_tcp_send возвращает ЧИСЛО отправленных байт при успехе и -1 при
    // ошибке. Поэтому успех — это >= 0, а не == 0 (было багом: любой
    // успешный send давал "!= 0" -> ложный провал handshake).
    if (send_handshake(&s, HS_CLIENT_HELLO, hello, (unsigned int)hlen) < 0) return 0;

    // body — 16 КБ. РАНЬШЕ был на стеке: вместе с цепочкой вызовов (ec с
    // десятками bn_t) переполнял стек ядра (0x90000, растёт вниз) и затирал
    // данные ядра (в т.ч. аудио-буферы HDA -> "сам заиграл звук"). static.
    static unsigned char body[16384];
    unsigned char hs;
    unsigned char server_random[32];
    rsa_pub_t server_pub;
    int got_cert = 0;
    int ecdhe = 0;                 // выбран ли ECDHE (по ServerKeyExchange)
    unsigned char server_ec_pub[65];
    for (;;) {
        int n = recv_handshake(&s, &hs, body, sizeof(body));
        if (n < 0) { serial_print("[tls] recv_handshake failed\n"); return 0; }
        serial_print("[tls] hs type="); serial_print_int(hs); serial_print(" len="); serial_print_int(n); serial_print("\n");
        if (hs == HS_SERVER_HELLO) {
            for (int i = 0; i < 32; i++) server_random[i] = body[2 + i];
            // Определяем выбранный шифр: тело = ver(2)+random(32)+sessid_len(1)+sessid+suite(2)...
            unsigned int sid_len = body[34];
            unsigned int su_pos = 35 + sid_len;
            if (su_pos + 1 < (unsigned int)n) {
                unsigned int suite = ((unsigned int)body[su_pos] << 8) | body[su_pos + 1];
                serial_print("[tls] server suite="); serial_print_int((int)suite); serial_print("\n");
                if (suite == 0xC02F) { ecdhe = 1; s.use_gcm = 1; }
                else if (suite == 0xC027) { ecdhe = 1; s.use_sha1 = 0; s.mac_len = 32; }
                else if (suite == 0xC013) { ecdhe = 1; s.use_sha1 = 1; s.mac_len = 20; }
                else if (suite == 0x003C) { ecdhe = 0; s.use_sha1 = 0; s.mac_len = 32; }
            }
        } else if (hs == HS_CERTIFICATE) {
            unsigned int cert_len = ((unsigned int)body[3] << 16) | ((unsigned int)body[4] << 8) | body[5];
            if (x509_extract_rsa_pubkey(body + 6, cert_len, &server_pub) != 0) {
                serial_print("[tls] x509 parse failed\n"); return 0;
            }
            got_cert = 1;
        } else if (hs == HS_SERVER_KEY_EXCHANGE) {
            // ECDHE: curve_type(1=3 named_curve), named_curve(2=0x0017),
            // pubkey_len(1), pubkey(65), [sig_alg(2), sig_len(2), sig].
            if (n < 4 || body[0] != 0x03) {
                serial_print("[tls] unexpected SKE (not named_curve)\n"); return 0;
            }
            unsigned int curve = ((unsigned int)body[1] << 8) | body[2];
            unsigned int pub_len = body[3];
            if (curve != 0x0017 || pub_len != 65 || 4 + pub_len > (unsigned int)n) {
                serial_print("[tls] SKE bad curve/pub\n"); return 0;
            }
            for (unsigned int i = 0; i < 65; i++) server_ec_pub[i] = body[4 + i];
            ecdhe = 1;
            serial_print("[tls] got SKE ECDHE secp256r1\n");
            // ---- Проверка подписи SKE (RFC 4492/5246) — защита от MITM ----
            // Подписывается: client_random(32)||server_random(32)||
            //   ServerECDHParams (= body[0 .. 4+pub_len): curve_type+named_curve+
            //   pubkey_len+pubkey). Хеш SHA-256, затем RSA-PKCS1-v1.5 verify
            //   ключом из сертификата (server_pub уже извлечён из Certificate).
            if (!got_cert) { serial_print("[tls] SKE before cert!\n"); return 0; }
            unsigned int params_len = 4 + pub_len;
            if (params_len + 4 > (unsigned int)n) {
                serial_print("[tls] SKE too short for sig\n"); return 0;
            }
            unsigned int sig_alg = ((unsigned int)body[params_len] << 8) | body[params_len + 1];
            unsigned int sig_len = ((unsigned int)body[params_len + 2] << 8) | body[params_len + 3];
            if (sig_alg != 0x0401) {   // rsa_pkcs1_sha256
                serial_print("[tls] SKE sig_alg unsupported="); serial_print_int((int)sig_alg); serial_print("\n"); return 0;
            }
            if (params_len + 4 + sig_len > (unsigned int)n || sig_len > 256) {
                serial_print("[tls] SKE sig_len bad\n"); return 0;
            }
            { unsigned char signed_data[32 + 32 + 128];
              for (int i = 0; i < 32; i++) signed_data[i] = client_random[i];
              for (int i = 0; i < 32; i++) signed_data[32 + i] = server_random[i];
              for (unsigned int i = 0; i < params_len; i++) signed_data[64 + i] = body[i];
              unsigned char shash[32];
              sha256(signed_data, 64 + params_len, shash);
              unsigned char sigbuf[256];
              for (int i = 0; i < 256; i++) sigbuf[i] = 0;
              for (unsigned int i = 0; i < sig_len; i++) sigbuf[256 - sig_len + i] = body[params_len + 4 + i];
              if (rsa_verify_pkcs1_sha256(&server_pub, sigbuf, shash) != 0) {
                  serial_print("[tls] SKE SIGNATURE VERIFY FAILED (MITM?)\n"); return 0;
              }
              serial_print("[tls] SKE signature OK\n");
            }
        } else if (hs == HS_SERVER_HELLO_DONE) {
            break;
        }
    }
    if (!got_cert) { serial_print("[tls] no cert\n"); return 0; }
    serial_print("[tls] got server cert + pubkey\n");

    unsigned char pms[48];
    if (ecdhe) {
        // ECDHE: наш keypair, шлём pubkey, premaster = общий секрет (32 байта X).
        unsigned char our_priv[32], our_pub[65], shared[32];
        ec_p256_keypair(our_priv, our_pub);
        unsigned char cke[1 + 65];
        cke[0] = 65;
        for (int i = 0; i < 65; i++) cke[1 + i] = our_pub[i];
        if (send_handshake(&s, HS_CLIENT_KEY_EXCHANGE, cke, 1 + 65) < 0) return 0;
        { extern unsigned int timer_ticks(void);
          unsigned int te0 = timer_ticks();
        if (ec_p256_shared(server_ec_pub, our_priv, shared) != 0) {
            serial_print("[tls] ECDHE shared failed\n"); return 0;
        }
          serial_print("[tls] ECDHE took ticks="); serial_print_int((int)(timer_ticks() - te0)); serial_print("\n");
        }
        for (int i = 0; i < 32; i++) pms[i] = shared[i];
        serial_print("[tls] ECDHE premaster ready\n");
    } else {
        pms[0] = TLS_VER_MAJOR; pms[1] = TLS_VER_MINOR;
        rand_bytes(pms + 2, 46);
        unsigned char enc_pms[256];
        if (rsa_encrypt_pkcs1(&server_pub, pms, 48, enc_pms) != 0) {
            serial_print("[tls] rsa encrypt failed\n"); return 0;
        }
        unsigned char cke[2 + 256];
        cke[0] = 0x01; cke[1] = 0x00;
        for (int i = 0; i < 256; i++) cke[2 + i] = enc_pms[i];
        if (send_handshake(&s, HS_CLIENT_KEY_EXCHANGE, cke, 2 + 256) < 0) return 0;
    }

    unsigned char seed[64];
    for (int i = 0; i < 32; i++) seed[i] = client_random[i];
    for (int i = 0; i < 32; i++) seed[32 + i] = server_random[i];
    unsigned char master[48];
    prf_sha256(pms, 48, "master secret", seed, 64, master, 48);

    unsigned char seed2[64];
    for (int i = 0; i < 32; i++) seed2[i] = server_random[i];
    for (int i = 0; i < 32; i++) seed2[32 + i] = client_random[i];
    // Key block. ВАЖНО: в TLS 1.2 PRF всегда SHA-256.
    //  GCM (0xC02F): client_key(16)|server_key(16)|client_salt(4)|server_salt(4).
    //  CBC:          client_mac(ml)|server_mac(ml)|client_key(16)|server_key(16)|
    //                client_iv(16)|server_iv(16).
    unsigned char kb[128];
    if (s.use_gcm) {
        prf_sha256(master, 48, "key expansion", seed2, 64, kb, 40);
        unsigned int o = 0;
        for (int i = 0; i < 16; i++) s.client_write_key[i] = kb[o + i]; o += 16;
        for (int i = 0; i < 16; i++) s.server_write_key[i] = kb[o + i]; o += 16;
        for (int i = 0; i < 4; i++) s.client_write_iv_gcm[i] = kb[o + i]; o += 4;
        for (int i = 0; i < 4; i++) s.server_write_iv_gcm[i] = kb[o + i]; o += 4;
    } else {
        unsigned int ml = s.mac_len;                 // 20 или 32
        unsigned int kb_len = 2 * ml + 2 * 16 + 2 * 16;
        prf_sha256(master, 48, "key expansion", seed2, 64, kb, kb_len);
        unsigned int o = 0;
        for (unsigned int i = 0; i < ml; i++) s.client_write_mac[i] = kb[o + i]; o += ml;
        for (unsigned int i = 0; i < ml; i++) s.server_write_mac[i] = kb[o + i]; o += ml;
        for (int i = 0; i < 16; i++) s.client_write_key[i] = kb[o + i]; o += 16;
        for (int i = 0; i < 16; i++) s.server_write_key[i] = kb[o + i]; o += 16;
        for (int i = 0; i < 16; i++) s.client_iv[i] = kb[o + i]; o += 16;
        for (int i = 0; i < 16; i++) s.server_iv[i] = kb[o + i]; o += 16;
    }

    unsigned char ccs = 1;
    if (send_record(&s, RT_CHANGE_CIPHER_SPEC, &ccs, 1) < 0) return 0;

    unsigned char hs_hash[32];
    sha256(g_hs_buf, g_hs_len, hs_hash);
    unsigned char fin_verify[12];
    prf_sha256(master, 48, "client finished", hs_hash, 32, fin_verify, 12);
    unsigned char fin_msg[4 + 12];
    fin_msg[0] = HS_FINISHED;
    fin_msg[1] = 0; fin_msg[2] = 0; fin_msg[3] = 12;
    for (int i = 0; i < 12; i++) fin_msg[4 + i] = fin_verify[i];
    // Наш Finished входит в handshake_messages (нужно для проверки
    // серверного Finished). send_encrypted НЕ трогает g_hs_buf — добавляем сами.
    hs_append(fin_msg, 4 + 12);
    if (send_encrypted(&s, RT_HANDSHAKE, fin_msg, 4 + 12) < 0) return 0;

    // Сервер отвечает: ChangeCipherSpec (открытый) + Finished (зашифрован).
    // Без чтения этих записей следующий tls_recv() съедал их как app-data
    // -> клиент получал мусор вместо HTTP.
    unsigned char stype, sbuf[128];
    int cn = recv_record(&s, &stype, sbuf, sizeof(sbuf));
    if (cn < 0 || stype != RT_CHANGE_CIPHER_SPEC) {
        serial_print("[tls] no server CCS (type="); serial_print_int(stype);
        serial_print(")\n"); return 0;
    }
    unsigned char sfin[64];
    int sn = recv_encrypted(&s, &stype, sfin, sizeof(sfin));
    if (sn < 0) { serial_print("[tls] server finished failed\n"); return 0; }
    // Проверка verify_data серверного Finished: PRF(master,"server finished",
    // SHA256(handshake_messages)).
    { unsigned char shash[32]; sha256(g_hs_buf, g_hs_len, shash);
      unsigned char sexp[12]; prf_sha256(master, 48, "server finished", shash, 32, sexp, 12);
      if (sn < 12) { serial_print("[tls] short server finished\n"); return 0; }
      for (int i = 0; i < 12; i++) if (sfin[4 + i] != sexp[i]) {
          serial_print("[tls] server finished verify MISMATCH\n"); return 0;
      }
    }

    s.ready = 1;
    serial_print("[tls] handshake done!\n");
    return &s;
}

int tls_send(tls_session* s, const void* data, unsigned int len) {
    if (!s || !s->ready) return -1;
    if (len > 16000) len = 16000;
    if (send_encrypted(s, RT_APPLICATION_DATA, (const unsigned char*)data, len) < 0) return -1;
    return (int)len;
}

int tls_recv(tls_session* s, void* buf, unsigned int max) {
    if (!s || !s->ready) return -1;
    unsigned char type, plain[TLS_MAX_RECORD];
    int n = recv_encrypted(s, &type, plain, sizeof(plain));
    if (n < 0) return -1;
    if (n > (int)max) n = (int)max;
    for (int i = 0; i < n; i++) ((unsigned char*)buf)[i] = plain[i];
    return n;
}

// Неблокирующий приём: одна попытка, 0 = данных пока нет.
int tls_recv_nb(tls_session* s, void* buf, unsigned int max) {
    if (!s || !s->ready) return -1;
    s->nonblock = 1;
    unsigned char type, plain[TLS_MAX_RECORD];
    int n = recv_encrypted(s, &type, plain, sizeof(plain));
    s->nonblock = 0;
    if (n < 0) return -1;
    if (n > (int)max) n = (int)max;
    for (int i = 0; i < n; i++) ((unsigned char*)buf)[i] = plain[i];
    return n;
}

void tls_close(tls_session* s) {
    if (!s) return;
    s->ready = 0;
}

int tls_ready(const tls_session* s) { return s && s->ready; }
