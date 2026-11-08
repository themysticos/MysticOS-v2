// tls/ec.h - ECDHE на secp256r1 (P-256). Своя реализация.
#ifndef TLS_EC_H
#define TLS_EC_H

// Вычислить ECDHE public key: out[65] = 0x04 || X(32) || Y(32).
// priv[32] - случайный приватный ключ (генерируется внутри, копируется в priv).
void ec_p256_keypair(unsigned char priv[32], unsigned char pub[65]);

// Общий секрет: shared = X-координата (32 байта) от (peer_pub * priv).
// peer_pub[65] = 0x04 || X || Y. priv[32] - наш приватный ключ.
// Возвращает 0 при успехе, -1 если точка невалидна / на бесконечности.
int ec_p256_shared(const unsigned char peer_pub[65],
                   const unsigned char priv[32],
                   unsigned char shared[32]);

#endif
