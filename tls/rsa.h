// tls/rsa.h - RSA-2048 (PKCS#1 v1.5) для TLS. Своя реализация.
#ifndef TLS_RSA_H
#define TLS_RSA_H

#include "bigint.h"

// RSA public key: n (модуль), e (экспонента).
typedef struct {
    bn_t n;
    bn_t e;
} rsa_pub_t;

// Шифрование блока (PKCS#1 v1.5): data до 245 байт (2048/8 - 11).
// Заполняет out 256 байт. Возвращает 0 при успехе, -1 при ошибке.
int rsa_encrypt_pkcs1(const rsa_pub_t* key, const unsigned char* data,
                      unsigned int len, unsigned char out[256]);

// Проверка подписи (PKCS#1 v1.5 + SHA-256). sig — 256 байт,
// hash — 32 байта SHA-256 от данных. Возвращает 0 при успехе, -1 при ошибке.
int rsa_verify_pkcs1_sha256(const rsa_pub_t* key, const unsigned char sig[256],
                            const unsigned char hash[32]);

#endif
