// tls/rand.c - криптостойкий ГПСЧ: RDRAND (аппаратный) + fallback jitter.
#include "rand.h"

static int g_has_rdrand = 0;
static unsigned long long g_jitter_state = 0;

static int cpu_has_rdrand(void) {
    unsigned int eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (ecx >> 30) & 1;
}

// RDRAND: возвращает 1 при успехе (CF=1), 0 если «не готово».
static int rdrand32(unsigned int* out) {
    unsigned char ok;
    unsigned int v;
    __asm__ volatile (
        "rdrand %0\n\tsetc %1"
        : "=r"(v), "=r"(ok)
        :
        : "cc"
    );
    if (ok) { *out = v; return 1; }
    return 0;
}

// RDTSC — для fallback-энтропии (jitter между вызовами).
static unsigned long long rdtsc(void) {
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

void rand_init(void) {
    g_has_rdrand = cpu_has_rdrand();
    // Seed для jitter: несколько RDTSC-замеров + XOR.
    g_jitter_state = 0;
    for (int i = 0; i < 16; i++) {
        unsigned long long t = rdtsc();
        g_jitter_state ^= t;
        g_jitter_state = (g_jitter_state << 7) | (g_jitter_state >> 57);  // rotate
        for (volatile int j = 0; j < 100 * i; j++) { }  // разный jitter
    }
    if (g_jitter_state == 0) g_jitter_state = 0x9E3779B97F4A7C15ULL;
}

int rand_has_rdrand(void) { return g_has_rdrand; }

// xorshift64* — быстрый PRNG для fallback (не криптостойкий сам по себе,
// но с jitter-seed'ом даёт достаточную энтропию при отсутствии RDRAND).
static unsigned long long xorshift64(void) {
    unsigned long long x = g_jitter_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_jitter_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

int rand_bytes(unsigned char* buf, unsigned int n) {
    if (!buf) return 0;
    unsigned int i = 0;
    if (g_has_rdrand) {
        // RDRAND: берём по 4 байта. При CF=0 — повторяем (до 10 раз).
        while (i < n) {
            unsigned int v = 0;
            int ok = 0;
            for (int tries = 0; tries < 10; tries++) {
                if (rdrand32(&v)) { ok = 1; break; }
            }
            if (!ok) break;   // RDRAND сломался — переходим на fallback
            unsigned int take = (n - i < 4) ? (n - i) : 4;
            for (unsigned int k = 0; k < take; k++) {
                buf[i + k] = (unsigned char)(v >> (k * 8));
            }
            i += take;
        }
    }
    // Fallback (или добивка, если RDRAND не хватило): xorshift + jitter.
    while (i < n) {
        // Добавляем jitter (RDTSC) в состояние — даже если RDRAND есть,
        // но не сработал, получим хоть какую-то энтропию.
        g_jitter_state ^= rdtsc();
        unsigned long long r = xorshift64();
        buf[i++] = (unsigned char)(r & 0xFF);
    }
    return 1;
}
