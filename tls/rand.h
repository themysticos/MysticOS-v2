// tls/rand.h - криптостойкий ГПСЧ для MysticOS (RDRAND + fallback).
#ifndef TLS_RAND_H
#define TLS_RAND_H

// Заполнить buf n случайными байтами. Возвращает 1 при успехе, 0 при неудаче.
int rand_bytes(unsigned char* buf, unsigned int n);

// Инициализация (проверка RDRAND, seed для fallback). Вызывать ОДИН раз
// при старте (после serial_init).
void rand_init(void);

// Для отладки: 1 если RDRAND доступен.
int rand_has_rdrand(void);

#endif
