// tls/x509.c - минимальный X.509 DER парсер. Вытаскивает RSA pubkey.
#include "x509.h"

// DER TLV: [tag (1)] [len (1 или 1+N)] [content (len)].
// Возвращает число прочитанных байт (tag+len+content) или -1.
// content указывает на начало content, *content_len = длина content.
static int der_read_tlv(const unsigned char* buf, unsigned int buflen,
                        unsigned char* tag,
                        const unsigned char** content, unsigned int* content_len) {
    if (buflen < 2) return -1;
    *tag = buf[0];
    unsigned int pos = 1;
    unsigned int len = buf[pos++];
    if (len & 0x80) {
        unsigned int n = len & 0x7F;
        if (n == 0 || n > 3 || pos + n > buflen) return -1;
        len = 0;
        for (unsigned int i = 0; i < n; i++) len = (len << 8) | buf[pos++];
    }
    if (pos + len > buflen) return -1;
    *content = buf + pos;
    *content_len = len;
    return (int)(pos + len);
}

int x509_extract_rsa_pubkey(const unsigned char* cert, unsigned int len, rsa_pub_t* out) {
    unsigned char tag;
    const unsigned char* content;
    unsigned int clen;
    int adv;

    // 1. Certificate ::= SEQUENCE
    adv = der_read_tlv(cert, len, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;

    // 2. TBSCertificate ::= SEQUENCE (первый элемент)
    adv = der_read_tlv(content, clen, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;

    const unsigned char* p = content;
    unsigned int rem = clen;

    // 3. Пропускаем поля TBSCertificate до subjectPublicKeyInfo.
    // Version [0] EXPLICIT (0xA0) — опционально.
    adv = der_read_tlv(p, rem, &tag, &content, &clen);
    if (adv < 0) return -1;
    if (tag == 0xA0) { p += adv; rem -= adv; }
    // serialNumber INTEGER (0x02)
    adv = der_read_tlv(p, rem, &tag, &content, &clen);
    if (adv < 0 || tag != 0x02) return -1;
    p += adv; rem -= adv;
    // signature AlgorithmIdentifier SEQUENCE (0x30)
    adv = der_read_tlv(p, rem, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;
    p += adv; rem -= adv;
    // issuer Name SEQUENCE (0x30)
    adv = der_read_tlv(p, rem, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;
    p += adv; rem -= adv;
    // validity SEQUENCE (0x30)
    adv = der_read_tlv(p, rem, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;
    p += adv; rem -= adv;
    // subject Name SEQUENCE (0x30)
    adv = der_read_tlv(p, rem, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;
    p += adv; rem -= adv;
    // subjectPublicKeyInfo SEQUENCE (0x30)
    adv = der_read_tlv(p, rem, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;
    const unsigned char* spki = content;
    unsigned int spki_len = clen;

    // 4. Внутри SPKI: AlgorithmIdentifier SEQUENCE, затем BIT STRING.
    adv = der_read_tlv(spki, spki_len, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;
    spki += adv; spki_len -= adv;
    // BIT STRING (0x03)
    adv = der_read_tlv(spki, spki_len, &tag, &content, &clen);
    if (adv < 0 || tag != 0x03 || clen < 2) return -1;
    // BIT STRING: [unused bits (1 байт)] [DER RSAPublicKey]
    if (content[0] != 0) return -1;   // ожидаем 0 unused bits
    const unsigned char* rsa_der = content + 1;
    unsigned int rsa_len = clen - 1;

    // 5. RSAPublicKey ::= SEQUENCE { modulus INTEGER, exponent INTEGER }
    adv = der_read_tlv(rsa_der, rsa_len, &tag, &content, &clen);
    if (adv < 0 || tag != 0x30) return -1;
    const unsigned char* rk = content;
    unsigned int rk_len = clen;

    // modulus INTEGER
    adv = der_read_tlv(rk, rk_len, &tag, &content, &clen);
    if (adv < 0 || tag != 0x02) return -1;
    const unsigned char* n_ptr = content;
    unsigned int n_len = clen;
    // Срезаем ведущие 0x00 (DER signed INTEGER padding для положительных).
    while (n_len > 1 && n_ptr[0] == 0) { n_ptr++; n_len--; }
    if (n_len > 256) return -1;
    if (bn_from_bytes(&out->n, n_ptr, n_len) != 0) return -1;
    rk += adv; rk_len -= adv;

    // exponent INTEGER
    adv = der_read_tlv(rk, rk_len, &tag, &content, &clen);
    if (adv < 0 || tag != 0x02) return -1;
    const unsigned char* e_ptr = content;
    unsigned int e_len = clen;
    while (e_len > 1 && e_ptr[0] == 0) { e_ptr++; e_len--; }
    if (e_len > 256) return -1;
    if (bn_from_bytes(&out->e, e_ptr, e_len) != 0) return -1;

    return 0;
}
