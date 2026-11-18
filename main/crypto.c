#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "crypto.h"

static const char *TAG = "crypto";

static void build_aad(const secure_msg_t *m, aad_t *aad)
{
    aad->magic = m->magic; aad->type = m->type;
    aad->src   = m->src;   aad->dst   = m->dst;
    aad->seq   = m->seq;   aad->len   = m->len;
}

esp_err_t crypto_seal(const uint8_t key[16], uint8_t src, uint32_t seq,
                      const plain_t *plain, size_t len, secure_msg_t *out)
{
    if (len > sizeof(plain_t)) return ESP_ERR_INVALID_SIZE;

    memset(out, 0, sizeof(*out));
    out->magic = 'N'; out->type = 2;
    out->src   = src;  out->dst  = NODE_HUB;
    out->seq   = seq;  out->len  = (uint16_t)len;

    out->nonce[0] = src;                    /* identidad, en claro y autenticada */
    memcpy(&out->nonce[1], &seq, 4);        /* seq creciente */
    esp_fill_random(&out->nonce[5], 7);     /* azar: el nonce nunca se repite */

    aad_t aad;
    build_aad(out, &aad);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    size_t produced = 0, tail = 0;

    if (ret == 0) ret = mbedtls_gcm_starts(&gcm, MBEDTLS_GCM_ENCRYPT,
                                          out->nonce, NEKO_NONCE_LEN);
    if (ret == 0) ret = mbedtls_gcm_update_ad(&gcm,
                                             (const uint8_t *)&aad, sizeof(aad));
    if (ret == 0) ret = mbedtls_gcm_update(&gcm, (const uint8_t *)plain, len,
                                           out->ct, len + 15, &produced);
    if (ret == 0) ret = mbedtls_gcm_finish(&gcm, out->ct + produced,
                                           sizeof(out->ct) - produced, &tail,
                                           out->tag, NEKO_TAG_LEN);
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        ESP_LOGE(TAG, "seal failed: -0x%04X", -ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t crypto_open(const uint8_t key[16], const secure_msg_t *in,
                      plain_t *out, size_t *out_len)
{
    if (in->magic != 'N' || in->len > sizeof(plain_t) || in->dst != NODE_HUB) {
        return ESP_ERR_INVALID_ARG;
    }

    aad_t aad;
    build_aad(in, &aad);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret == 0) {
        ret = mbedtls_gcm_auth_decrypt(&gcm, in->len,
                                       in->nonce, NEKO_NONCE_LEN,
                                       (const uint8_t *)&aad, sizeof(aad),
                                       in->tag, NEKO_TAG_LEN,
                                       in->ct, (uint8_t *)out);
    }
    mbedtls_gcm_free(&gcm);

    if (ret != 0) return ESP_ERR_INVALID_ARG;  /* tag invalido: descartar */
    *out_len = in->len;
    return ESP_OK;
}
