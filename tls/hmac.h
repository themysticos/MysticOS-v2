// tls/hmac.h - HMAC-SHA256 (RFC 2104). Своя реализация.
#ifndef TLS_HMAC_H
#define TLS_HMAC_H

// HMAC-SHA256(key, msg) -> out[32].
void hmac_sha256(const void* key, unsigned int key_len,
                 const void* msg, unsigned int msg_len,
                 unsigned char out[32]);

// HMAC-SHA1(key, msg) -> out[20]. Для TLS 1.2 CBC-SHA1 (0xC013).
void hmac_sha1(const void* key, unsigned int key_len,
               const void* msg, unsigned int msg_len,
               unsigned char out[20]);

#endif
