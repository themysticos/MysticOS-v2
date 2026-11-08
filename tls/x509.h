// tls/x509.h - минимальный X.509 парсер (только RSA pubkey). Своя реализация.
#ifndef TLS_X509_H
#define TLS_X509_H

#include "rsa.h"

// Распарсить DER-сертификат, извлечь RSA public key (n, e).
// cert — указатель на DER, len — длина. Возвращает 0 при успехе, -1 при ошибке.
int x509_extract_rsa_pubkey(const unsigned char* cert, unsigned int len, rsa_pub_t* out);

#endif
