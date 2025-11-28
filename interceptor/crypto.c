#include "crypto.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t link_key[16];
static int key_initialized = 0;

int crypto_init_link_key(const char *key_hex) {
    if (strlen(key_hex) != 32) {
        ERROR_PRINT("Invalid link key length (expected 32 hex chars)");
        return -1;
    }
    
    for (int i = 0; i < 16; i++) {
        char byte_str[3] = {key_hex[i*2], key_hex[i*2+1], '\0'};
        link_key[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }
    
    key_initialized = 1;
    INFO_PRINT("Link key initialized");
    return 0;
}

int crypto_decrypt_payload(const uint8_t *encrypted, size_t len, uint8_t *decrypted) {
    if (!key_initialized) {
        ERROR_PRINT("Link key not initialized");
        return -1;
    }
    
    DEBUG_PRINT("Decrypting %zu bytes (E0 cipher not fully implemented - copying data)", len);
    memcpy(decrypted, encrypted, len);
    
    return len;
}

int crypto_encrypt_payload(const uint8_t *plain, size_t len, uint8_t *encrypted) {
    if (!key_initialized) {
        ERROR_PRINT("Link key not initialized");
        return -1;
    }
    
    DEBUG_PRINT("Encrypting %zu bytes (E0 cipher not fully implemented - copying data)", len);
    memcpy(encrypted, plain, len);
    
    return len;
}
