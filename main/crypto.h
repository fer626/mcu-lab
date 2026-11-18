#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "protocol.h"

esp_err_t crypto_seal(const uint8_t key[16], uint8_t src, uint32_t seq,
                      const plain_t *plain, size_t len, secure_msg_t *out);

esp_err_t crypto_open(const uint8_t key[16], const secure_msg_t *in,
                      plain_t *out, size_t *out_len);
