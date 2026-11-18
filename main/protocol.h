#pragma once
#include <stdint.h>

#define NEKO_NAME_LEN  24
#define NEKO_TEXT_LEN  96
#define NEKO_NONCE_LEN 12
#define NEKO_TAG_LEN   16

typedef enum {
    NODE_HUB = 0,
    NODE_TINACO = 1,
    NODE_BOMBA = 2,
    NODE_CISTERNA = 3,
    NODE_COUNT = 4,
} node_id_t;

/* Lo que se cifra: nombre + texto = 120 bytes. */
typedef struct __attribute__((packed)) {
    char name[NEKO_NAME_LEN];
    char text[NEKO_TEXT_LEN];
} plain_t;

/* AAD: cabecera autenticada. Incluye len para que no recorten el mensaje. */
typedef struct __attribute__((packed)) {
    uint8_t  magic, type, src, dst;   /* 4 */
    uint32_t seq;                     /* 4 */
    uint16_t len;                     /* 2 -> total 10 bytes */
} aad_t;

/* Trama en el aire: 158 bytes (cabe holgado en ESP_NOW_MAX_DATA_LEN = 250). */
typedef struct __attribute__((packed)) {
    uint8_t  magic;                  /* 'N' */
    uint8_t  type;                   /* 1 = HELLO, 2 = REPLY */
    uint8_t  src, dst;
    uint32_t seq;
    uint8_t  nonce[NEKO_NONCE_LEN];  /* src(1) + seq(4) + azar(7) */
    uint16_t len;                    /* bytes utiles de plain_t */
    uint8_t  ct[sizeof(plain_t)];
    uint8_t  tag[NEKO_TAG_LEN];
} secure_msg_t;
