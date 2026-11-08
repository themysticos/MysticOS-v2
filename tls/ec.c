// tls/ec.c - ECDHE на secp256r1 (P-256). Якобианы координаты:
// инверсия (modpow) выполняется ОДИН раз в конце скалярного умножения,
// а не на каждое сложение/удвоение. Это в сотни раз быстрее аффинного.
#include "ec.h"
#include "bigint.h"
#include "rand.h"

// --- secp256r1 параметры (hex, big-endian) ---
static const char* P_HEX =
    "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF";
static const char* A_HEX =
    "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC";
static const char* B_HEX =
    "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B";
static const char* GX_HEX =
    "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296";
static const char* GY_HEX =
    "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5";
static const char* N_HEX =
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void bn_from_hex(bn_t* a, const char* hex) {
    unsigned char buf[64];
    int len = 0;
    while (hex[len]) len++;
    int bytes = len / 2;
    if (bytes > 64) bytes = 64;
    for (int i = 0; i < bytes; i++)
        buf[i] = (unsigned char)((hexval(hex[2 * i]) << 4) | hexval(hex[2 * i + 1]));
    bn_from_bytes(a, buf, (unsigned int)bytes);
}

// Якобиана: (x, y, z) ~ аффинно (x/z^2, y/z^3). z==0 => бесконечность.
typedef struct { bn_t x, y, z; int inf; } pt_t;

static bn_t P, A, B, GX, GY, N, ONE;
static int g_inited = 0;

static void ec_init(void) {
    if (g_inited) return;
    bn_from_hex(&P, P_HEX);
    bn_from_hex(&A, A_HEX);
    bn_from_hex(&B, B_HEX);
    bn_from_hex(&GX, GX_HEX);
    bn_from_hex(&GY, GY_HEX);
    bn_from_hex(&N, N_HEX);
    bn_from_u32(&ONE, 1);
    g_inited = 1;
}

static void fp_add(bn_t* r, const bn_t* a, const bn_t* b) {
    unsigned int carry = bn_add(r, a, b);
    if (carry || bn_cmp(r, &P) >= 0) bn_sub(r, r, &P);
}

static void fp_sub(bn_t* r, const bn_t* a, const bn_t* b) {
    bn_t t;
    if (bn_cmp(a, b) >= 0) bn_sub(r, a, b);
    else { bn_sub(&t, b, a); bn_sub(r, &P, &t); }
}

static void fp_mul(bn_t* r, const bn_t* a, const bn_t* b) {
    bn_modmul(r, a, b, &P);
}

// r = a^-1 mod P (Ферма: a^(P-2)) — дорого, вызываем РЕДКО.
static void fp_inv(bn_t* r, const bn_t* a) {
    bn_t e, two;
    bn_from_u32(&two, 2);
    bn_sub(&e, &P, &two);
    bn_modpow(r, a, &e, &P);
}

// Удвоение в якобианах (a = -3 для P-256).
static void pt_double(pt_t* r, const pt_t* q) {
    if (q->inf || bn_is_zero(&q->z) || bn_is_zero(&q->y)) { r->inf = 1; bn_zero(&r->z); return; }
    bn_t delta, gamma, beta, alpha, t1, t2, X3, Y3, Z3;
    fp_mul(&delta, &q->z, &q->z);        // Z^2
    fp_mul(&gamma, &q->y, &q->y);        // Y^2
    fp_mul(&beta,  &q->x, &gamma);       // X*Y^2
    fp_sub(&t1, &q->x, &delta);          // X - Z^2
    fp_add(&t2, &q->x, &delta);          // X + Z^2
    fp_mul(&t1, &t1, &t2);               // X^2 - Z^4
    fp_add(&alpha, &t1, &t1);            // 2
    fp_add(&alpha, &alpha, &t1);         // 3*(X^2 - Z^4)
    fp_mul(&X3, &alpha, &alpha);         // alpha^2
    fp_add(&t1, &beta, &beta);           // 2beta
    fp_add(&t1, &t1, &t1);               // 4beta
    fp_add(&t1, &t1, &t1);               // 8beta
    fp_sub(&X3, &X3, &t1);               // X3 = alpha^2 - 8beta
    fp_add(&t1, &q->y, &q->z);
    fp_mul(&Z3, &t1, &t1);               // (Y+Z)^2
    fp_sub(&Z3, &Z3, &gamma);
    fp_sub(&Z3, &Z3, &delta);            // Z3
    fp_add(&t1, &beta, &beta);           // 2beta
    fp_add(&t1, &t1, &t1);               // 4beta
    fp_sub(&t1, &t1, &X3);               // 4beta - X3
    fp_mul(&t1, &alpha, &t1);
    fp_mul(&t2, &gamma, &gamma);         // gamma^2
    fp_add(&t2, &t2, &t2);
    fp_add(&t2, &t2, &t2);
    fp_add(&t2, &t2, &t2);               // 8gamma^2
    fp_sub(&Y3, &t1, &t2);               // Y3
    r->x = X3; r->y = Y3; r->z = Z3; r->inf = 0;
}

// Сложение якобиан: r = q + s.
static void pt_add(pt_t* r, const pt_t* q, const pt_t* s) {
    if (q->inf || bn_is_zero(&q->z)) { *r = *s; return; }
    if (s->inf || bn_is_zero(&s->z)) { *r = *q; return; }
    bn_t Z1Z1, Z2Z2, U1, U2, S1, S2, H, rr, I, J, V, t1, t2, X3, Y3, Z3;
    fp_mul(&Z1Z1, &q->z, &q->z);
    fp_mul(&Z2Z2, &s->z, &s->z);
    fp_mul(&U1, &q->x, &Z2Z2);
    fp_mul(&U2, &s->x, &Z1Z1);
    fp_mul(&t1, &s->z, &Z2Z2);
    fp_mul(&S1, &q->y, &t1);             // Y1*Z2^3
    fp_mul(&t2, &q->z, &Z1Z1);
    fp_mul(&S2, &s->y, &t2);             // Y2*Z1^3
    fp_sub(&H, &U2, &U1);
    fp_sub(&rr, &S2, &S1);
    fp_add(&rr, &rr, &rr);               // r = 2(S2-S1)
    if (bn_is_zero(&H)) {
        if (bn_is_zero(&rr)) { pt_double(r, q); return; }
        r->inf = 1; bn_zero(&r->z); return;
    }
    fp_add(&t1, &H, &H);                 // 2H
    fp_mul(&I, &t1, &t1);                // (2H)^2
    fp_mul(&J, &H, &I);
    fp_mul(&V, &U1, &I);
    fp_mul(&X3, &rr, &rr);
    fp_sub(&X3, &X3, &J);
    fp_add(&t1, &V, &V);
    fp_sub(&X3, &X3, &t1);               // X3 = r^2 - J - 2V
    fp_sub(&t1, &V, &X3);
    fp_mul(&t1, &rr, &t1);
    fp_mul(&t2, &S1, &J);
    fp_add(&t2, &t2, &t2);
    fp_sub(&Y3, &t1, &t2);               // Y3 = r(V-X3) - 2*S1*J
    fp_add(&t1, &q->z, &s->z);
    fp_mul(&Z3, &t1, &t1);
    fp_sub(&Z3, &Z3, &Z1Z1);
    fp_sub(&Z3, &Z3, &Z2Z2);
    fp_mul(&Z3, &Z3, &H);                // Z3 = ((Z1+Z2)^2-Z1Z1-Z2Z2)*H
    r->x = X3; r->y = Y3; r->z = Z3; r->inf = 0;
}

// Перевод якобианы в аффинны (одна инверсия).
static int pt_to_affine(bn_t* ox, bn_t* oy, const pt_t* p) {
    if (p->inf || bn_is_zero(&p->z)) return -1;
    bn_t zi, zi2, zi3;
    fp_inv(&zi, &p->z);
    fp_mul(&zi2, &zi, &zi);
    fp_mul(&zi3, &zi2, &zi);
    fp_mul(ox, &p->x, &zi2);
    fp_mul(oy, &p->y, &zi3);
    return 0;
}

// R = k*Q. Q — аффинна (z=1). Справа-налево: acc+=base при бите; base*=2.
static void pt_mul(pt_t* r, const pt_t* q, const bn_t* k) {
    pt_t acc; acc.inf = 1; bn_zero(&acc.z);
    pt_t base = *q;
    base.z = ONE; base.inf = 0;
    int bits = bn_bitlen(k);
    for (int i = 0; i < bits; i++) {
        if (bn_getbit(k, i)) pt_add(&acc, &acc, &base);
        if (i + 1 < bits) pt_double(&base, &base);
    }
    *r = acc;
}

void ec_p256_keypair(unsigned char priv[32], unsigned char pub[65]) {
    ec_init();
    bn_t k;
    do {
        rand_bytes(priv, 32);
        priv[0] &= 0x7F;                 // < 2^255 < N
        bn_from_bytes(&k, priv, 32);
    } while (bn_is_zero(&k));

    pt_t g; g.x = GX; g.y = GY; g.z = ONE; g.inf = 0;
    pt_t pubpt;
    pt_mul(&pubpt, &g, &k);
    bn_t ax, ay;
    if (pt_to_affine(&ax, &ay, &pubpt) != 0) { pub[0] = 0x04; return; }
    pub[0] = 0x04;
    bn_to_bytes(&ax, pub + 1, 32);
    bn_to_bytes(&ay, pub + 33, 32);
}

int ec_p256_shared(const unsigned char peer_pub[65],
                   const unsigned char priv[32],
                   unsigned char shared[32]) {
    ec_init();
    if (peer_pub[0] != 0x04) return -1;
    pt_t peer;
    bn_from_bytes(&peer.x, peer_pub + 1, 32);
    bn_from_bytes(&peer.y, peer_pub + 33, 32);
    peer.z = ONE; peer.inf = 0;
    // Проверка: точка на кривой? y^2 == x^3 + a*x + b (mod P)
    bn_t lhs, rhs, x3, ax;
    fp_mul(&lhs, &peer.y, &peer.y);
    fp_mul(&x3, &peer.x, &peer.x);
    fp_mul(&x3, &x3, &peer.x);
    fp_mul(&ax, &A, &peer.x);
    fp_add(&rhs, &x3, &ax);
    fp_add(&rhs, &rhs, &B);
    if (bn_cmp(&lhs, &rhs) != 0) return -1;

    bn_t k;
    bn_from_bytes(&k, priv, 32);
    pt_t res;
    pt_mul(&res, &peer, &k);
    bn_t rx, ry;
    if (pt_to_affine(&rx, &ry, &res) != 0) return -1;
    bn_to_bytes(&rx, shared, 32);
    return 0;
}
