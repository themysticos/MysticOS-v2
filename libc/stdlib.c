// libc/stdlib.c - аллокатор и общие функции (общая libc MysticOS).
//
// malloc/free: простой free-list (first-fit) с объединением соседей.
// Арена задаётся libc_init_heap(base, size) в _start программы.
#include "../include/libc/stdlib.h"
#include "../include/libc/string.h"   // memset/memcpy
#include "../include/ms/system.h"    // ms_exit для exit()

// ---- Аллокатор ----
typedef struct block {
    size_t size;        // размер данных (без заголовка)
    int    free;        // 1 = свободен
    struct block* next; // следующий блок
} block_t;

static block_t* g_head = 0;
#define ALIGN4(x) (((x) + 3u) & ~3u)

void libc_init_heap(void* base, size_t size) {
    if (size < sizeof(block_t)) { g_head = 0; return; }
    g_head = (block_t*)base;
    g_head->size = size - sizeof(block_t);
    g_head->free = 1;
    g_head->next = 0;
}

void* malloc(size_t n) {
    if (!g_head || n == 0) return 0;
    n = ALIGN4(n);
    for (block_t* b = g_head; b; b = b->next) {
        if (b->free && b->size >= n) {
            // Разбить, если хватает на новый блок.
            if (b->size >= n + sizeof(block_t) + 16) {
                block_t* nb = (block_t*)((unsigned char*)b + sizeof(block_t) + n);
                nb->size = b->size - n - sizeof(block_t);
                nb->free = 1;
                nb->next = b->next;
                b->size = n;
                b->next = nb;
            }
            b->free = 0;
            return (unsigned char*)b + sizeof(block_t);
        }
    }
    return 0;   // нет памяти
}

void free(void* p) {
    if (!p || !g_head) return;
    block_t* b = (block_t*)((unsigned char*)p - sizeof(block_t));
    b->free = 1;
    // Объединить соседние свободные блоки.
    for (block_t* x = g_head; x; x = x->next) {
        while (x->next && x->free && x->next->free) {
            x->size += sizeof(block_t) + x->next->size;
            x->next = x->next->next;
        }
    }
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free(p); return 0; }
    block_t* b = (block_t*)((unsigned char*)p - sizeof(block_t));
    if (b->size >= n) return p;   // уже хватает
    void* np = malloc(n);
    if (!np) return 0;
    memcpy(np, p, b->size);
    free(p);
    return np;
}

// ---- Прочее ----
int atoi(const char* s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return sign * v;
}

double atof(const char* s) {
    double v = 0; int sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++; double f = 0.1;
        while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f *= 0.1; s++; }
    }
    return sign * v;
}

int abs(int v) { return v < 0 ? -v : v; }

void exit(int code) { (void)code; ms_exit(code); for(;;){} }

int system(const char* cmd) { (void)cmd; return -1; }
