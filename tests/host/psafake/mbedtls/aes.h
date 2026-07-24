/* psafake: minimal <mbedtls/aes.h> recording double (see ../psafake.h — the
 * fake does NO crypto; it records the key bits/mode and copies input to
 * output, with injectable failures). */
#ifndef PSAFAKE_MBEDTLS_AES_H
#define PSAFAKE_MBEDTLS_AES_H

#include <stddef.h>
#include <stdint.h>

#include "psafake.h"

#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0

typedef struct {
	int inited;
} mbedtls_aes_context;

void mbedtls_aes_init(mbedtls_aes_context *ctx);
void mbedtls_aes_free(mbedtls_aes_context *ctx);
int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key,
			   unsigned int keybits);
int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx, int mode, const unsigned char input[16],
			  unsigned char output[16]);

#endif /* PSAFAKE_MBEDTLS_AES_H */
