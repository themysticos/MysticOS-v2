// tls/sha1.c - SHA-1 (FIPS 180-4). Используется как HMAC в TLS 1.2 CBC-SHA1.
#include "sha1.h"

static unsigned int rol(unsigned int x, int n) {
    return (x << n) | (x >> (32 - n));
}

void sha1_init(sha1_ctx* c) {
    c->h[0] = 0x67452301u;
    c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->len = 0;
    c->buflen = 0;
}

static void sha1_block(sha1_ctx* c, const unsigned char* p) {
    unsigned int w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((unsigned int)p[i*4] << 24) | ((unsigned int)p[i*4+1] << 16) |
               ((unsigned int)p[i*4+2] << 8) | (unsigned int)p[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    unsigned int a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        unsigned int f, k;
        if (i < 20)      { f = (b & cc) | ((~b) & d);        k = 0x5A827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ cc ^ d;                   k = 0xCA62C1D6u; }
        unsigned int t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

void sha1_update(sha1_ctx* c, const void* data, unsigned int len) {
    const unsigned char* p = (const unsigned char*)data;
    c->len += len;
    while (len > 0) {
        unsigned int take = 64 - c->buflen;
        if (take > len) take = len;
        for (unsigned int i = 0; i < take; i++) c->buf[c->buflen + i] = p[i];
        c->buflen += take; p += take; len -= take;
        if (c->buflen == 64) { sha1_block(c, c->buf); c->buflen = 0; }
    }
}

void sha1_final(sha1_ctx* c, unsigned char out[20]) {
    unsigned long long bits = c->len * 8;
    unsigned char pad = 0x80;
    sha1_update(c, &pad, 1);
    unsigned char zero = 0;
    while (c->buflen != 56) sha1_update(c, &zero, 1);
    unsigned char lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (unsigned char)(bits >> (56 - i*8));
    sha1_update(c, lenb, 8);
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (unsigned char)(c->h[i] >> 24);
        out[i*4+1] = (unsigned char)(c->h[i] >> 16);
        out[i*4+2] = (unsigned char)(c->h[i] >> 8);
        out[i*4+3] = (unsigned char)(c->h[i]);
    }
}

void sha1(const void* data, unsigned int len, unsigned char out[20]) {
    sha1_ctx c; sha1_init(&c); sha1_update(&c, data, len); sha1_final(&c, out);
}
