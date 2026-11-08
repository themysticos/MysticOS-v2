// tls/aes.h - AES-128 (FIPS-197) + CBC режим. Своя реализация.
#ifndef TLS_AES_H
#define TLS_AES_H

typedef struct {
    unsigned int rk[44];   // 11 раундовых ключей x 4 слова
} aes128_ctx;

void aes128_init(aes128_ctx* ctx, const unsigned char key[16]);
void aes128_encrypt_block(const aes128_ctx* ctx, const unsigned char in[16], unsigned char out[16]);
void aes128_decrypt_block(const aes128_ctx* ctx, const unsigned char in[16], unsigned char out[16]);

// CBC: шифрует/дешифрует len байт (len кратен 16). IV обновляется
// (для следующего вызова можно передать тот же iv).
void aes128_cbc_encrypt(const aes128_ctx* ctx, unsigned char iv[16],
                        const unsigned char* in, unsigned char* out, unsigned int len);
void aes128_cbc_decrypt(const aes128_ctx* ctx, unsigned char iv[16],
                        const unsigned char* in, unsigned char* out, unsigned int len);

#endif
