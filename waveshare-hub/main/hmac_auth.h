#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void hmac_sign(const char *path, char *hmac_out, char *nonce_out);

#ifdef __cplusplus
}
#endif
