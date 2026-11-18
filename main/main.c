/*
 * app - ESP-NOW "hello/mundo" demo con mensajes AUTENTICADOS y CIFRADOS
 *
 * Capa 1 (enlace): ESP-NOW con PMK global + LMK por par cuando la MAC del peer
 *                  esta registrada en secrets.h. En modo prueba el hub acepta
 *                  cualquier MAC autenticada y el peer se registra dinamicamente.
 * Capa 2 (app):    AES-128-GCM (mbedTLS 3.x) con AAD sobre la cabecera, seq y
 *                  anti-replay. Es la que garantiza confidencialidad y que un
 *                  C3 ajeno no pueda suplantar a otro.
 *
 * ESP32-S3 (hub)  : envia "hola" por ESP-NOW y muestra en el OLED el nombre y
 *                  mensaje de cada nodo que responde. Solo muestra mensajes
 *                  cuyo tag GCM es valido y cuyo seq es nuevo.
 * ESP32-C3 (nodo) : responde "<recibido> mundo" cifrado con su clave de rol.
 *                  El nombre por defecto es c3-XXXX y se cambia con `name`.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board.h"
#include "crypto.h"
#include "protocol.h"
#include "ssd1306.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define USING_DEV_KEYS 0
#else
#include "secrets_dev.h"
#define USING_DEV_KEYS 1
#endif

#define RX_QUEUE_LEN   4
#define NEKO_NAME_LEN  24

#define MSG_HELLO      1
#define MSG_REPLY      2

static const char *TAG = "app";

typedef struct {
    uint8_t src[6];
    int len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} rx_packet_t;

static QueueHandle_t s_rx_queue;

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_size) {
        n = dst_size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const uint8_t *key_for_node(uint8_t id)
{
    switch (id) {
        case NODE_TINACO:   return K_TINACO;
        case NODE_CISTERNA: return K_CISTERNA;
        case NODE_BOMBA:    return K_BOMBA;
        default:            return NULL;
    }
}

static const uint8_t *lmk_for_node(uint8_t id)
{
    switch (id) {
        case NODE_TINACO:   return LMK_TINACO;
        case NODE_CISTERNA: return LMK_CISTERNA;
        case NODE_BOMBA:    return LMK_BOMBA;
        default:            return NULL;
    }
}

static bool mac_is_zero(const uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) return false;
    }
    return true;
}

static void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void log_boot_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  boot");
#if CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "  role        : hub (ESP32-S3)");
#elif CONFIG_IDF_TARGET_ESP32C3
    ESP_LOGI(TAG, "  role        : node (ESP32-C3)");
#endif
    ESP_LOGI(TAG, "  target      : %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "  cores       : %d", chip.cores);
    ESP_LOGI(TAG, "  revision    : %d", chip.revision);
    ESP_LOGI(TAG, "  MAC (STA)   : " MACSTR, MAC2STR(mac));
    ESP_LOGI(TAG, "  idf         : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "  free heap   : %u bytes", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "  crypto      : AES-128-GCM + AAD + seq (mbedTLS)");
#if USING_DEV_KEYS
    ESP_LOGW(TAG, "  KEYS        : DEV (inseguras) - configura GitHub Secrets");
#else
    ESP_LOGI(TAG, "  KEYS        : reales (secrets.h)");
#endif
#if CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "  oled        : SSD1306 %dx%d @0x%02X (SDA=GPIO%d, SCL=GPIO%d)",
             OLED_WIDTH, OLED_HEIGHT, OLED_I2C_ADDR, OLED_SDA_GPIO, OLED_SCL_GPIO);
    ESP_LOGI(TAG, "  period      : %d ms", SEND_PERIOD_MS);
#elif CONFIG_IDF_TARGET_ESP32C3
    ESP_LOGI(TAG, "  tx power    : %d (%.2f dBm)", TX_POWER_QDBM, TX_POWER_QDBM * 0.25);
#endif
    ESP_LOGI(TAG, "==============================================");
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* ESP-NOW needs the radio listening, so keep power save off. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(TX_POWER_QDBM));
}

#if CONFIG_IDF_TARGET_ESP32S3
/* ------------------------------- hub ------------------------------------ */

#define MAX_NODES     8
#define VISIBLE_NODES 4   /* 2 text rows each, 8 rows total */

typedef struct {
    bool     used;
    uint8_t  mac[6];
    char     name[NEKO_NAME_LEN];
    char     text[NEKO_TEXT_LEN];
    int64_t  last_seen_us;
} node_entry_t;

static const uint8_t s_broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static node_entry_t s_nodes[MAX_NODES];
static uint32_t s_last_seq[NODE_COUNT];
static bool s_oled_ok;
static bool s_link_encryption;

static void hub_render(void);

static void hub_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    ESP_LOGD(TAG, "TX -> broadcast: %s",
             status == ESP_NOW_SEND_SUCCESS ? "ok" : "failed");
}

static void hub_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    rx_packet_t pkt = { 0 };
    memcpy(pkt.src, info->src_addr, sizeof(pkt.src));
    pkt.len = len < ESP_NOW_MAX_DATA_LEN ? len : ESP_NOW_MAX_DATA_LEN;
    memcpy(pkt.data, data, pkt.len);

    if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full, dropping packet");
    }
}

/* Registra al peer con su LMK para cifrar tambien la capa de enlace.
 * En modo prueba (MAC en cero) no se conoce la MAC de antemano, asi que el
 * peer se da de alta dinamicamente y la capa 2 (GCM) es la que protege. */
static bool hub_ensure_peer(const uint8_t mac[6], uint8_t node_id)
{
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    if (!s_link_encryption) {
        return true;   /* modo prueba: GCM protege el payload */
    }

    const uint8_t *lmk = lmk_for_node(node_id);
    if (lmk == NULL) return false;

    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, mac, 6);
    memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not add peer " MACSTR ": %s",
                 MAC2STR(mac), esp_err_to_name(err));
        return false;
    }
    return true;
}

static node_entry_t *hub_find_or_add(const uint8_t mac[6])
{
    node_entry_t *free_slot = NULL;

    for (int i = 0; i < MAX_NODES; i++) {
        if (s_nodes[i].used && memcmp(s_nodes[i].mac, mac, 6) == 0) {
            return &s_nodes[i];
        }
        if (!s_nodes[i].used && free_slot == NULL) {
            free_slot = &s_nodes[i];
        }
    }

    if (free_slot != NULL) {
        free_slot->used = true;
        memcpy(free_slot->mac, mac, 6);
    }
    return free_slot;
}

static void hub_handle_rx(const rx_packet_t *pkt)
{
    secure_msg_t m;
    if (pkt->len != (int)sizeof(m)) {
        ESP_LOGW(TAG, "RX <- " MACSTR ": bad length %d (want %u)",
                 MAC2STR(pkt->src), pkt->len, (unsigned)sizeof(m));
        return;
    }
    memcpy(&m, pkt->data, sizeof(m));

    if (m.magic != 'N' || m.type != MSG_REPLY || m.dst != NODE_HUB) {
        ESP_LOGW(TAG, "RX <- " MACSTR ": bad header (magic=0x%02X type=%u dst=%u)",
                 MAC2STR(pkt->src), m.magic, m.type, m.dst);
        return;
    }
    if (m.src == NODE_HUB || m.src >= NODE_COUNT) {
        ESP_LOGW(TAG, "RX <- " MACSTR ": bad src %u", MAC2STR(pkt->src), m.src);
        return;
    }

    const uint8_t *key = key_for_node(m.src);
    if (key == NULL) {
        ESP_LOGW(TAG, "RX <- " MACSTR ": no key for src %u", MAC2STR(pkt->src), m.src);
        return;
    }

    /* Anti-replay: se descarta cualquier seq ya visto para ese nodo. */
    if (m.seq <= s_last_seq[m.src]) {
        ESP_LOGW(TAG, "RX <- " MACSTR ": replay seq=%u (last=%u)",
                 MAC2STR(pkt->src), (unsigned)m.seq, (unsigned)s_last_seq[m.src]);
        return;
    }

    plain_t plain;
    size_t plain_len = 0;
    if (crypto_open(key, &m, &plain, &plain_len) != ESP_OK) {
        ESP_LOGW(TAG, "RX <- " MACSTR ": AUTH FAILED src=%u seq=%u (descartado)",
                 MAC2STR(pkt->src), m.src, (unsigned)m.seq);
        return;   /* tag invalido: no se muestra en el OLED */
    }

    if (!hub_ensure_peer(pkt->src, m.src)) {
        return;
    }
    s_last_seq[m.src] = m.seq;

    node_entry_t *e = hub_find_or_add(pkt->src);
    if (e == NULL) {
        ESP_LOGW(TAG, "node table full, ignoring " MACSTR, MAC2STR(pkt->src));
        return;
    }

    plain.name[NEKO_NAME_LEN - 1] = '\0';
    plain.text[NEKO_TEXT_LEN - 1] = '\0';
    copy_str(e->name, sizeof(e->name), plain.name);
    copy_str(e->text, sizeof(e->text), plain.text);
    e->last_seen_us = esp_timer_get_time();

    ESP_LOGI(TAG, "AUTH OK <- %s (" MACSTR ") seq=%u: \"%s\"",
             e->name, MAC2STR(pkt->src), (unsigned)m.seq, e->text);

    hub_render();
}

static void hub_render(void)
{
    if (!s_oled_ok) {
        return;
    }

    /* Sort node indices by most-recently seen first (insertion sort). */
    int order[MAX_NODES];
    int count = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_nodes[i].used) {
            order[count++] = i;
        }
    }
    for (int i = 1; i < count; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && s_nodes[order[j]].last_seen_us < s_nodes[key].last_seen_us) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    ssd1306_clear();
    if (count == 0) {
        ssd1306_draw_text(0, 0, "ESP-NOW hub");
        ssd1306_draw_text(0, 16, "esperando nodos...");
    } else {
        for (int i = 0; i < count && i < VISIBLE_NODES; i++) {
            node_entry_t *e = &s_nodes[order[i]];
            ssd1306_draw_text(0, i * 16, e->name);
            ssd1306_draw_text(0, i * 16 + 8, e->text);
        }
    }
    ssd1306_flush();
}

static void hub_broadcast(const char *msg)
{
    /* El "hola" es publico: solo dispara la respuesta cifrada del nodo. */
    esp_err_t err = esp_now_send(s_broadcast, (const uint8_t *)msg, strlen(msg) + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send failed: %s", esp_err_to_name(err));
    }
}

static void hub_task(void *arg)
{
    (void)arg;
    TickType_t last_send = xTaskGetTickCount() - pdMS_TO_TICKS(SEND_PERIOD_MS);

    while (1) {
        rx_packet_t pkt;
        if (xQueueReceive(s_rx_queue, &pkt, pdMS_TO_TICKS(200)) == pdTRUE) {
            hub_handle_rx(&pkt);
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_send >= pdMS_TO_TICKS(SEND_PERIOD_MS)) {
            ESP_LOGI(TAG, "TX -> broadcast: \"hola\" (trigger, sin datos)");
            hub_broadcast("hola");
            last_send = now;
        }
    }
}

#elif CONFIG_IDF_TARGET_ESP32C3
/* ------------------------------- node ----------------------------------- */

#define NODE_ROLE_ID NODE_TINACO   /* modo prueba: un solo C3 con clave de tinaco */

static char s_name[NEKO_NAME_LEN];
static uint32_t s_seq;

static void node_name_from_mac(char *out, size_t n)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, n, "c3-%02X%02X", mac[4], mac[5]);
}

static void node_name_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_name);

    if (nvs_open("node", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, "name", s_name, &len) != ESP_OK || s_name[0] == '\0') {
            node_name_from_mac(s_name, sizeof(s_name));
        }
        nvs_close(h);
    } else {
        node_name_from_mac(s_name, sizeof(s_name));
    }

    s_seq = 0;
    if (nvs_open("node", NVS_READONLY, &h) == ESP_OK) {
        uint32_t stored = 0;
        size_t slen = sizeof(stored);
        if (nvs_get_u32(h, "seq", &stored) == ESP_OK && slen == sizeof(stored)) {
            s_seq = stored;
        }
        nvs_close(h);
    }

    ESP_LOGI(TAG, "node name: %s | role=%u | seq=%u", s_name, NODE_ROLE_ID,
             (unsigned)s_seq);
}

static void node_seq_save(void)
{
    nvs_handle_t h;
    if (nvs_open("node", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "seq", s_seq);
        nvs_commit(h);
        nvs_close(h);
    }
}

static int cmd_name(int argc, char **argv)
{
    if (argc < 2) {
        printf("name: %s\n", s_name);
        return 0;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open("node", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        printf("nvs open failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    if (strcmp(argv[1], "-") == 0 || strcmp(argv[1], "reset") == 0) {
        nvs_erase_key(h, "name");
        nvs_commit(h);
        nvs_close(h);
        node_name_from_mac(s_name, sizeof(s_name));
        printf("name reset to %s\n", s_name);
        return 0;
    }

    char buf[NEKO_NAME_LEN] = { 0 };
    size_t used = 0;
    for (int i = 1; i < argc; i++) {
        size_t l = strlen(argv[i]);
        size_t sep = (i > 1) ? 1 : 0;
        if (used + sep + l >= sizeof(buf)) {
            printf("name too long (max %d)\n", (int)sizeof(buf) - 1);
            nvs_close(h);
            return 1;
        }
        if (sep) {
            buf[used++] = ' ';
        }
        memcpy(buf + used, argv[i], l);
        used += l;
        buf[used] = '\0';
    }

    err = nvs_set_str(h, "name", buf);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        printf("save failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    copy_str(s_name, sizeof(s_name), buf);
    printf("name set to %s\n", s_name);
    return 0;
}

static void node_repl_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "c3> ";
    repl_cfg.max_cmdline_length = 96;

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dev_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t dev_cfg = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&dev_cfg, &repl_cfg, &repl));
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    esp_console_dev_uart_config_t dev_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&dev_cfg, &repl_cfg, &repl));
#else
    ESP_LOGW(TAG, "no serial console; name stays at the MAC default");
    return;
#endif

    const esp_console_cmd_t name_cmd = {
        .command = "name",
        .help = "Show or set this node's name ('name -' resets to the MAC default)",
        .hint = "[text|-]",
        .func = &cmd_name,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&name_cmd));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

static void node_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    rx_packet_t pkt = { 0 };
    memcpy(pkt.src, info->src_addr, sizeof(pkt.src));
    pkt.len = len < ESP_NOW_MAX_DATA_LEN ? len : ESP_NOW_MAX_DATA_LEN;
    memcpy(pkt.data, data, pkt.len);

    if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full, dropping packet");
    }
}

static bool node_add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;   /* modo prueba: no conocemos la LMK del hub */

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not add peer " MACSTR ": %s",
                 MAC2STR(mac), esp_err_to_name(err));
        return false;
    }
    return true;
}

static void node_task(void *arg)
{
    (void)arg;
    rx_packet_t pkt;
    const uint8_t *key = key_for_node(NODE_ROLE_ID);

    while (1) {
        if (xQueueReceive(s_rx_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char text[ESP_NOW_MAX_DATA_LEN + 1];
        int n = pkt.len < ESP_NOW_MAX_DATA_LEN ? pkt.len : ESP_NOW_MAX_DATA_LEN;
        memcpy(text, pkt.data, n);
        text[n] = '\0';

        /* Solo respondemos al disparo publico del hub. */
        if (strcmp(text, "hola") != 0) {
            ESP_LOGD(TAG, "ignore non-hello frame (%d bytes)", pkt.len);
            continue;
        }
        if (key == NULL || !node_add_peer(pkt.src)) {
            continue;
        }

        plain_t plain;
        memset(&plain, 0, sizeof(plain));
        copy_str(plain.name, sizeof(plain.name), s_name);

        const char suffix[] = " mundo";
        size_t tlen = strlen(text);
        if (tlen > sizeof(plain.text) - sizeof(suffix)) {
            tlen = sizeof(plain.text) - sizeof(suffix);
        }
        memcpy(plain.text, text, tlen);
        memcpy(plain.text + tlen, suffix, sizeof(suffix));
        size_t plain_len = tlen + sizeof(suffix) - 1;

        secure_msg_t reply;
        s_seq++;
        if (crypto_seal(key, NODE_ROLE_ID, s_seq, &plain, plain_len, &reply) != ESP_OK) {
            ESP_LOGE(TAG, "crypto_seal failed");
            s_seq--;
            continue;
        }
        node_seq_save();

        ESP_LOGI(TAG, "TX -> " MACSTR ": %s: \"%s\" seq=%u (cifrado)",
                 MAC2STR(pkt.src), plain.name, plain.text, (unsigned)s_seq);

        esp_err_t err = esp_now_send(pkt.src, (const uint8_t *)&reply, sizeof(reply));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "send failed: %s", esp_err_to_name(err));
        }
    }
}

#else
#error "This demo only defines roles for ESP32-S3 (hub) and ESP32-C3 (node)"
#endif

static void espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());

#if CONFIG_IDF_TARGET_ESP32S3
    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, s_broadcast, sizeof(peer.peer_addr));
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    /* Solo se activa si las 3 MACs estan configuradas en secrets.h. */
    s_link_encryption = !mac_is_zero(MAC_TINACO) && !mac_is_zero(MAC_CISTERNA)
                        && !mac_is_zero(MAC_BOMBA);
    if (s_link_encryption) {
        ESP_ERROR_CHECK(esp_now_set_pmk(ESPNOW_PMK));
        const uint8_t *macs[3] = { MAC_TINACO, MAC_CISTERNA, MAC_BOMBA };
        for (uint8_t id = NODE_TINACO; id <= NODE_BOMBA; id++) {
            if (!hub_ensure_peer(macs[id - 1], id)) {
                ESP_LOGE(TAG, "could not pre-register node %u", id);
            }
        }
        ESP_LOGI(TAG, "link-layer AES-CCM enabled for 3 known peers");
    } else {
        ESP_LOGW(TAG, "test mode: unknown MACs, link encryption off (GCM protects payload)");
    }

    ESP_ERROR_CHECK(esp_now_register_send_cb(hub_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(hub_recv_cb));
#elif CONFIG_IDF_TARGET_ESP32C3
    ESP_ERROR_CHECK(esp_now_register_recv_cb(node_recv_cb));
#endif
}

void app_main(void)
{
    nvs_init();
    log_boot_info();

    wifi_init();

    s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_packet_t));
    configASSERT(s_rx_queue != NULL);

    espnow_init();

    ESP_LOGI(TAG, "ESP-NOW ready on channel %d", ESPNOW_CHANNEL);

#if CONFIG_IDF_TARGET_ESP32S3
    s_oled_ok = (ssd1306_init() == ESP_OK);
    if (!s_oled_ok) {
        ESP_LOGW(TAG, "OLED unavailable (check I2C wiring: SDA=GPIO%d, SCL=GPIO%d)",
                 OLED_SDA_GPIO, OLED_SCL_GPIO);
    }
    hub_render();
    xTaskCreate(hub_task, "hub", 4096, NULL, 5, NULL);
#elif CONFIG_IDF_TARGET_ESP32C3
    node_name_load();
    xTaskCreate(node_task, "node", 4096, NULL, 5, NULL);
    node_repl_start();
#endif
}
