// tls/bigint.h - 2048-битные числа для RSA. Своя реализация.
#ifndef TLS_BIGINT_H
#define TLS_BIGINT_H

// 2048 бит = 64 слова по 32 бита (little-endian: w[0] — младшее).
#define BN_WORDS 64

typedef struct {
    unsigned int w[BN_WORDS];
} bn_t;

// 2n слов (для mul).
typedef struct {
    unsigned int w[BN_WORDS * 2];
} bn2_t;

void bn_zero(bn_t* a);
void bn_from_u32(bn_t* a, unsigned int v);
int  bn_cmp(const bn_t* a, const bn_t* b);
unsigned int bn_add(bn_t* r, const bn_t* a, const bn_t* b);
unsigned int bn_sub(bn_t* r, const bn_t* a, const bn_t* b);
int  bn_is_zero(const bn_t* a);
int  bn_bitlen(const bn_t* a);
int  bn_getbit(const bn_t* a, int i);

void bn_mul(bn2_t* r, const bn_t* a, const bn_t* b);
void bn_divmod(bn_t* q, bn_t* r, const bn_t* a, const bn_t* b);
void bn_modpow(bn_t* r, const bn_t* base, const bn_t* exp, const bn_t* mod);
// r = (a*b) mod m  (обёртка над mul+reduce; нужно для ECC)
void bn_modmul(bn_t* r, const bn_t* a, const bn_t* b, const bn_t* m);

int bn_from_bytes(bn_t* a, const unsigned char* buf, unsigned int len);
int bn_to_bytes(const bn_t* a, unsigned char* buf, unsigned int len);

#endif
