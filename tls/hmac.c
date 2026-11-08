// tls/hmac.c - HMAC-SHA256 (RFC 2104). Своя реализация.
#include "hmac.h"
#include "sha256.h"
#include "sha1.h"

#define IPAD 0x36
#define OPAD 0x5C
#define BLOCK 64

void hmac_sha256(const void* key, unsigned int key_len,
                 const void* msg, unsigned int msg_len,
                 unsigned char out[32]) {
    unsigned char k[BLOCK];
    // Если ключ длиннее блока — хешируем его. Иначе дополняем нулями.
    if (key_len > BLOCK) {
        sha256(key, key_len, k);
        for (unsigned int i = 32; i < BLOCK; i++) k[i] = 0;
    } else {
        const unsigned char* p = (const unsigned char*)key;
        for (unsigned int i = 0; i < key_len; i++) k[i] = p[i];
        for (unsigned int i = key_len; i < BLOCK; i++) k[i] = 0;
    }

    unsigned char ipad[BLOCK], opad[BLOCK];
    for (int i = 0; i < BLOCK; i++) { ipad[i] = k[i] ^ IPAD; opad[i] = k[i] ^ OPAD; }

    sha256_ctx c;
    unsigned char inner[32];
    sha256_init(&c);
    sha256_update(&c, ipad, BLOCK);
    sha256_update(&c, msg, msg_len);
    sha256_final(&c, inner);

    sha256_init(&c);
    sha256_update(&c, opad, BLOCK);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
}

void hmac_sha1(const void* key, unsigned int key_len,
               const void* msg, unsigned int msg_len,
               unsigned char out[20]) {
    unsigned char k[BLOCK];
    if (key_len > BLOCK) {
        sha1(key, key_len, k);
        for (unsigned int i = 20; i < BLOCK; i++) k[i] = 0;
    } else {
        const unsigned char* p = (const unsigned char*)key;
        for (unsigned int i = 0; i < key_len; i++) k[i] = p[i];
        for (unsigned int i = key_len; i < BLOCK; i++) k[i] = 0;
    }

    unsigned char ipad[BLOCK], opad[BLOCK];
    for (int i = 0; i < BLOCK; i++) { ipad[i] = k[i] ^ IPAD; opad[i] = k[i] ^ OPAD; }

    sha1_ctx c;
    unsigned char inner[20];
    sha1_init(&c);
    sha1_update(&c, ipad, BLOCK);
    sha1_update(&c, msg, msg_len);
    sha1_final(&c, inner);

    sha1_init(&c);
    sha1_update(&c, opad, BLOCK);
    sha1_update(&c, inner, 20);
    sha1_final(&c, out);
}
