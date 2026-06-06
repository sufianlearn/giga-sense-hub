/**
 * @file hmac_auth.cpp
 * @brief HMAC-SHA256 auth for camera node requests.
 */
#include "hmac_auth.h"
#include "definitions.h"
#include "mbedtls/md.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

#define HMAC_SECRET_KEY    "gsh_k3y_2026!s3cure"
#define AUTH_HEADER        "X-GSH-Auth"
#define AUTH_NONCE_HEADER  "X-GSH-Nonce"

void hmac_sign(const char *path, char *hmac_out, char *nonce_out)
{
    /* Generate random nonce */
    uint32_t r = esp_random();
    snprintf(nonce_out, 16, "%08lx", (unsigned long)r);

    /* Build message: path + nonce */
    char msg[256];
    snprintf(msg, sizeof(msg), "%s%s", path, nonce_out);

    /* HMAC-SHA256 */
    unsigned char hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)HMAC_SECRET_KEY, strlen(HMAC_SECRET_KEY));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)msg, strlen(msg));
    mbedtls_md_hmac_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    /* Hex encode */
    for (int i = 0; i < 32; i++) {
        sprintf(hmac_out + i * 2, "%02x", hash[i]);
    }
    hmac_out[64] = '\0';
}
