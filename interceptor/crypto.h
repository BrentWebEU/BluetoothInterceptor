#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

int crypto_init_link_key(const char *key_hex);
int crypto_decrypt_payload(const uint8_t *encrypted, size_t len, uint8_t *decrypted);
int crypto_encrypt_payload(const uint8_t *plain, size_t len, uint8_t *encrypted);

#endif
