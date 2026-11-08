// tls/sha256.h - SHA-256 (FIPS 180-4) для MysticOS. Своя реализация, MIT-стиль.
#ifndef TLS_SHA256_H
#define TLS_SHA256_H

typedef struct {
    unsigned int  h[8];       // текущее состояние
    unsigned long long len;   // всего обработано байт
    unsigned char buf[64];    // текущий блок (незавершённый)
    unsigned int  buflen;     // сколько в buf
} sha256_ctx;

void sha256_init(sha256_ctx* c);
void sha256_update(sha256_ctx* c, const void* data, unsigned int len);
void sha256_final(sha256_ctx* c, unsigned char out[32]);

// Однократный хеш (удобно для тестов).
void sha256(const void* data, unsigned int len, unsigned char out[32]);

#endif
