// tls/sha1.h - SHA-1 (FIPS 180-4). Нужен для TLS 1.2 CBC-SHA1 (0xC013).
// SHA-1 НЕ для безопасности — только как MAC в legacy-шифрах TLS.
#ifndef TLS_SHA1_H
#define TLS_SHA1_H

typedef struct {
    unsigned int  h[5];
    unsigned long long len;
    unsigned char buf[64];
    unsigned int  buflen;
} sha1_ctx;

void sha1_init(sha1_ctx* c);
void sha1_update(sha1_ctx* c, const void* data, unsigned int len);
void sha1_final(sha1_ctx* c, unsigned char out[20]);
void sha1(const void* data, unsigned int len, unsigned char out[20]);

#endif
