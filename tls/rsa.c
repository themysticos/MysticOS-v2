// tls/rsa.c - RSA-2048 (PKCS#1 v1.5). Своя реализация.
#include "rsa.h"
#include "sha256.h"

extern int rand_bytes(unsigned char* buf, unsigned int n);

#define RSA_BYTES 256

// PKCS#1 v1.5 encryption: 0x00 || 0x02 || PS (ненулевые) || 0x00 || data
// PS минимум 8 байт. Для 2048 бит: data <= 245 байт.
int rsa_encrypt_pkcs1(const rsa_pub_t* key, const unsigned char* data,
                      unsigned int len, unsigned char out[256]) {
    if (len > RSA_BYTES - 11) return -1;
    unsigned char em[RSA_BYTES];
    em[0] = 0x00; em[1] = 0x02;
    unsigned int pslen = RSA_BYTES - 3 - len;
    // PS: случайные ненулевые байты.
    unsigned int i = 0;
    while (i < pslen) {
        unsigned char b;
        rand_bytes(&b, 1);
        if (b != 0) em[2 + i++] = b;
    }
    em[2 + pslen] = 0x00;
    for (unsigned int k = 0; k < len; k++) em[3 + pslen + k] = data[k];
    // m = OS2IP(em), c = m^e mod n, out = I2OSP(c, 256)
    bn_t m, c;
    if (bn_from_bytes(&m, em, RSA_BYTES) != 0) return -1;
    bn_modpow(&c, &m, &key->e, &key->n);
    if (bn_to_bytes(&c, out, RSA_BYTES) != 0) return -1;
    return 0;
}

// PKCS#1 v1.5 verify (SHA-256):
// em = 0x00 || 0x01 || FF..FF || 0x00 || DigestInfo(SHA-256) || hash
// DigestInfo для SHA-256 (19 байт префикса).
static const unsigned char SHA256_DIGESTINFO[19] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
};

int rsa_verify_pkcs1_sha256(const rsa_pub_t* key, const unsigned char sig[256],
                            const unsigned char hash[32]) {
    bn_t s, m;
    if (bn_from_bytes(&s, sig, RSA_BYTES) != 0) return -1;
    if (bn_cmp(&s, &key->n) >= 0) return -1;   // s >= n — невалидно
    bn_modpow(&m, &s, &key->e, &key->n);
    unsigned char em[RSA_BYTES];
    if (bn_to_bytes(&m, em, RSA_BYTES) != 0) return -1;
    // Проверяем em: 0x00 0x01 FF..FF 0x00 || DigestInfo || hash
    if (em[0] != 0x00 || em[1] != 0x01) return -1;
    unsigned int i = 2;
    while (i < RSA_BYTES && em[i] == 0xFF) i++;
    if (i < 10) return -1;   // минимум 8 FF-байт
    if (em[i] != 0x00) return -1;
    i++;
    if (i + 19 + 32 != RSA_BYTES) return -1;
    for (int k = 0; k < 19; k++) if (em[i + k] != SHA256_DIGESTINFO[k]) return -1;
    for (int k = 0; k < 32; k++) if (em[i + 19 + k] != hash[k]) return -1;
    return 0;
}
