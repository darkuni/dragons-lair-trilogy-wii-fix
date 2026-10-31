/* AES-128 (CBC) and SHA-1 - small self-contained implementations. */
#ifndef DLT_CRYPTO_H
#define DLT_CRYPTO_H
#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t rk[176]; } aes128_ctx;
void aes128_init(aes128_ctx *c, const uint8_t key[16]);
void aes128_encrypt_block(const aes128_ctx *c, const uint8_t in[16], uint8_t out[16]);
void aes128_decrypt_block(const aes128_ctx *c, const uint8_t in[16], uint8_t out[16]);
/* in-place allowed; len must be a multiple of 16 */
void aes128_cbc_encrypt(const aes128_ctx *c, const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len);
void aes128_cbc_decrypt(const aes128_ctx *c, const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len);

typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } sha1_ctx;
void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t len);
void sha1_final(sha1_ctx *c, uint8_t out[20]);
void sha1(const void *data, size_t len, uint8_t out[20]);
void sha1_hex(const uint8_t d[20], char out[41]);

int crypto_selftest(void);
#endif
