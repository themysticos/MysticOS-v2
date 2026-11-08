// tls/bigint.c - 2048-битные числа. Своя реализация.
#include "bigint.h"

void bn_zero(bn_t* a) { for (int i = 0; i < BN_WORDS; i++) a->w[i] = 0; }

void bn_from_u32(bn_t* a, unsigned int v) {
    bn_zero(a); a->w[0] = v;
}

int bn_cmp(const bn_t* a, const bn_t* b) {
    for (int i = BN_WORDS - 1; i >= 0; i--) {
        if (a->w[i] < b->w[i]) return -1;
        if (a->w[i] > b->w[i]) return 1;
    }
    return 0;
}

unsigned int bn_add(bn_t* r, const bn_t* a, const bn_t* b) {
    unsigned long long carry = 0;
    for (int i = 0; i < BN_WORDS; i++) {
        unsigned long long s = (unsigned long long)a->w[i] + b->w[i] + carry;
        r->w[i] = (unsigned int)(s & 0xFFFFFFFFu);
        carry = s >> 32;
    }
    return (unsigned int)carry;
}

unsigned int bn_sub(bn_t* r, const bn_t* a, const bn_t* b) {
    unsigned long long borrow = 0;
    for (int i = 0; i < BN_WORDS; i++) {
        unsigned long long d = (unsigned long long)a->w[i] - b->w[i] - borrow;
        r->w[i] = (unsigned int)(d & 0xFFFFFFFFu);
        borrow = (d >> 32) ? 1 : 0;
    }
    return (unsigned int)borrow;
}

int bn_is_zero(const bn_t* a) {
    for (int i = 0; i < BN_WORDS; i++) if (a->w[i]) return 0;
    return 1;
}

int bn_bitlen(const bn_t* a) {
    for (int i = BN_WORDS - 1; i >= 0; i--) {
        if (a->w[i]) {
            unsigned int v = a->w[i];
            int b = 0;
            while (v) { v >>= 1; b++; }
            return i * 32 + b;
        }
    }
    return 0;
}

int bn_getbit(const bn_t* a, int i) {
    if (i < 0 || i >= BN_WORDS * 32) return 0;
    return (a->w[i / 32] >> (i % 32)) & 1;
}

// r = a*b (в bn2_t, 2n слов). Школьное умножение.
void bn_mul(bn2_t* r, const bn_t* a, const bn_t* b) {
    for (int i = 0; i < BN_WORDS * 2; i++) r->w[i] = 0;
    for (int i = 0; i < BN_WORDS; i++) {
        if (a->w[i] == 0) continue;
        unsigned long long carry = 0;
        for (int j = 0; j < BN_WORDS; j++) {
            unsigned long long t = (unsigned long long)a->w[i] * b->w[j]
                                 + r->w[i + j] + carry;
            r->w[i + j] = (unsigned int)(t & 0xFFFFFFFFu);
            carry = t >> 32;
        }
        r->w[i + BN_WORDS] += (unsigned int)carry;
    }
}

// q = a/b, r = a mod b. Побитовое длинное деление (простое, надёжное).
void bn_divmod(bn_t* q, bn_t* r, const bn_t* a, const bn_t* b) {
    bn_t qq, rr;
    bn_zero(&qq);
    bn_zero(&rr);
    int blen = bn_bitlen(a);
    for (int i = blen - 1; i >= 0; i--) {
        // rr = rr << 1
        unsigned int carry = 0;
        for (int k = 0; k < BN_WORDS; k++) {
            unsigned int nv = (rr.w[k] << 1) | carry;
            carry = rr.w[k] >> 31;
            rr.w[k] = nv;
        }
        // rr |= bit a[i]
        rr.w[0] |= (unsigned int)bn_getbit(a, i);
        // if rr >= b: rr -= b, qq bit i = 1
        if (bn_cmp(&rr, b) >= 0) {
            bn_sub(&rr, &rr, b);
            qq.w[i / 32] |= (1u << (i % 32));
        }
    }
    if (q) *q = qq;
    if (r) *r = rr;
}

// Редукция 2n-словного числа prod по модулю mod (n слов). Бит-в-бит:
// remainder = 0; для каждого бита от старшего к младшему: сдвиг влево,
// |бит, если >= mod — вычесть mod.
// Аккумулятор на BN_WORDS+1 слов: после сдвига rr может быть до 2*mod,
// а mod у RSA ~2^2048 — 2*mod не влезает в 2048 бит. Лишнее слово решает.
static void bn2_mod(bn_t* out, const bn2_t* prod, const bn_t* mod) {
    unsigned int rr[BN_WORDS + 1];
    for (int k = 0; k <= BN_WORDS; k++) rr[k] = 0;
    int top = BN_WORDS * 2 * 32 - 1;
    while (top >= 0 && !((prod->w[top / 32] >> (top % 32)) & 1)) top--;
    for (int i = top; i >= 0; i--) {
        // rr <<= 1 (учитывая лишнее слово)
        unsigned int carry = 0;
        for (int k = 0; k <= BN_WORDS; k++) {
            unsigned int nv = (rr[k] << 1) | carry;
            carry = rr[k] >> 31;
            rr[k] = nv;
        }
        if ((prod->w[i / 32] >> (i % 32)) & 1) rr[0] |= 1u;
        // Сравнение с mod (mod — BN_WORDS слов, старшее слово mod[BN_WORDS] = 0)
        int ge = 0;
        if (rr[BN_WORDS] != 0) ge = 1;
        else {
            ge = 0;
            for (int k = BN_WORDS - 1; k >= 0; k--) {
                if (rr[k] > mod->w[k]) { ge = 1; break; }
                if (rr[k] < mod->w[k]) { ge = 0; break; }
            }
        }
        if (ge) {
            // rr -= mod (с заимствованием через лишнее слово)
            unsigned long long borrow = 0;
            for (int k = 0; k < BN_WORDS; k++) {
                unsigned long long d = (unsigned long long)rr[k] - mod->w[k] - borrow;
                rr[k] = (unsigned int)(d & 0xFFFFFFFFu);
                borrow = (d >> 32) ? 1 : 0;
            }
            rr[BN_WORDS] -= (unsigned int)borrow;
        }
    }
    for (int k = 0; k < BN_WORDS; k++) out->w[k] = rr[k];
}

// r = base^exp mod mod. Бинарный метод (square-and-multiply).
void bn_modpow(bn_t* r, const bn_t* base, const bn_t* exp, const bn_t* mod) {
    bn_t result, b, q;
    bn_from_u32(&result, 1);
    bn_divmod(&q, &b, base, mod);   // b = base mod mod
    int elen = bn_bitlen(exp);
    for (int i = 0; i < elen; i++) {
        if (bn_getbit(exp, i)) {
            bn2_t prod;
            bn_mul(&prod, &result, &b);
            bn_t rem;
            bn2_mod(&rem, &prod, mod);
            result = rem;
        }
        if (i + 1 < elen) {   // последний квадрат не нужен
            bn2_t sq;
            bn_mul(&sq, &b, &b);
            bn_t rem2;
            bn2_mod(&rem2, &sq, mod);
            b = rem2;
        }
    }
    *r = result;
}

// r = (a*b) mod m. Обёртка: умножение в bn2_t + редукция (bn2_mod).
void bn_modmul(bn_t* r, const bn_t* a, const bn_t* b, const bn_t* m) {
    bn2_t prod;
    bn_mul(&prod, a, b);
    bn2_mod(r, &prod, m);
}

int bn_from_bytes(bn_t* a, const unsigned char* buf, unsigned int len) {
    if (len > BN_WORDS * 4) return -1;
    bn_zero(a);
    // buf в big-endian: последний байт — младший.
    for (unsigned int i = 0; i < len; i++) {
        unsigned int byte = buf[len - 1 - i];
        unsigned int word = i / 4, sh = (i % 4) * 8;
        a->w[word] |= byte << sh;
    }
    return 0;
}

int bn_to_bytes(const bn_t* a, unsigned char* buf, unsigned int len) {
    if (len > BN_WORDS * 4) return -1;
    for (unsigned int i = 0; i < len; i++) {
        unsigned int word = i / 4, sh = (i % 4) * 8;
        buf[len - 1 - i] = (unsigned char)(a->w[word] >> sh);
    }
    return 0;
}
