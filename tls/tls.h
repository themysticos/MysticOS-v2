// tls/tls.h - TLS 1.2 client (RSA + AES-128-CBC + SHA-256). Своя реализация.
#ifndef TLS_TLS_H
#define TLS_TLS_H

// Максимальная запись TLS (для буферов).
#define TLS_MAX_RECORD 16384

// TLS-сессия. Хранит ключи, состояния, номера последовательностей.
typedef struct tls_session tls_session;

// Открыть TCP + сделать TLS handshake с host (SNI) / ip:port.
// Возвращает указатель на сессию или NULL при ошибке.
tls_session* tls_connect(const unsigned char* ip, unsigned short port, const char* sni_host);

// Отправить данные (application data) — шифрует, шлёт записью TLS.
// Возвращает число отправленных байт или -1.
int tls_send(tls_session* s, const void* data, unsigned int len);

// Принять данные (application data) — расшифровывает, копирует до max.
// Возвращает число байт (0 если пока ничего), или -1 при ошибке/закрытии.
int tls_recv(tls_session* s, void* buf, unsigned int max);

// Неблокирующий приём: одна попытка. 0 = данных пока нет, >0 = байты, -1 = ошибка.
int tls_recv_nb(tls_session* s, void* buf, unsigned int max);

// Закрыть сессию (close_notify + FIN).
void tls_close(tls_session* s);

// Статус: 1 если handshake прошёл, 0 если нет.
int tls_ready(const tls_session* s);

#endif
