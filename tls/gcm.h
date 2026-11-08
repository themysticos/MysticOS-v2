// tls/gcm.h - AES-128-GCM (AEAD, RFC 5288/5116). Своя реализация.
// GCM = AES-CTR (шифр) + GHASH (аутентификация, GF(2^128)).
// Для TLS 1.2 nonce = salt(4)||explicit(8) = 12 байт; тег 16 байт.
#ifndef TLS_GCM_H
#define TLS_GCM_H

#include "aes.h"

typedef struct {
    aes128_ctx aes;
    unsigned char H[16];   // H = AES_K(0^128)
} gcm_ctx;

void gcm_init(gcm_ctx* g, const unsigned char key[16]);

// Шифрование: out[0..len) = ciphertext, tag[16] = аутентиф. тег.
void gcm_encrypt(gcm_ctx* g, const unsigned char nonce[12],
                 const unsigned char* aad, unsigned int aadlen,
                 const unsigned char* plain, unsigned int len,
                 unsigned char* out, unsigned char tag[16]);

// Расшифровка + проверка тега. 0 = успех, -1 = тег не сошёлся.
int gcm_decrypt(gcm_ctx* g, const unsigned char nonce[12],
                const unsigned char* aad, unsigned int aadlen,
                const unsigned char* ct, unsigned int len,
                unsigned char* out, const unsigned char tag[16]);

#endif
