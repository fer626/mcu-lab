/*
 * app - ESP-NOW "hello" demo with an OLED hub
 *
 * ESP32-S3 (hub)  : broadcasts "hola" every SEND_PERIOD_MS and shows, on an
 *                   SSD1306 I2C OLED, the name of every node that answers
 *                   together with the full message it sent.
 * ESP32-C3 (node) : replies "<received> mundo" tagged with its name. The name
 *                   defaults to "c3-XXXX" (last two bytes of the STA MAC) and
 *                   can be overridden from the serial console with `name`.
 *
 * No Wi-Fi AP, router or credentials are needed. Both boards use ESP-NOW on a
 * fixed channel. The hub talks to the broadcast address, so no MAC has to be
 * hard-coded; the node answers to the source address it sees.
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
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board.h"
#include "ssd1306.h"

#define RX_QUEUE_LEN   4
#define NEKO_NAME_LEN  24
#define NEKO_TEXT_LEN  200
#define NEKO_MAGIC     0x4E   /* 'N' */

static const char *TAG = "app";

/* Wire format of a node -> hub message. */
typedef struct __attribute__((packed)) {
    uint8_t magic;
    char    name[NEKO_NAME_LEN];
    char    text[NEKO_TEXT_LEN];
} node_msg_t;

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
    /* Lower TX power: less current, less heat. */
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
static bool s_oled_ok;

static void hub_render(void);

static void hub_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    ESP_LOGI(TAG, "TX -> broadcast: %s",
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
    node_msg_t m = { 0 };
    int n = pkt->len;
    if (n > (int)sizeof(m)) {
        n = sizeof(m);
    }
    memcpy(&m, pkt->data, n);

    if (m.magic != NEKO_MAGIC) {
        ESP_LOGW(TAG, "RX <- " MACSTR ": bad magic 0x%02X",
                 MAC2STR(pkt->src), m.magic);
        return;
    }
    m.name[NEKO_NAME_LEN - 1] = '\0';
    m.text[NEKO_TEXT_LEN - 1] = '\0';

    node_entry_t *e = hub_find_or_add(pkt->src);
    if (e == NULL) {
        ESP_LOGW(TAG, "node table full, ignoring " MACSTR, MAC2STR(pkt->src));
        return;
    }

    copy_str(e->name, sizeof(e->name), m.name);
    copy_str(e->text, sizeof(e->text), m.text);
    e->last_seen_us = esp_timer_get_time();

    ESP_LOGI(TAG, "RX <- %s (" MACSTR "): \"%s\"",
             e->name, MAC2STR(pkt->src), e->text);

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
    esp_err_t err = esp_now_send(s_broadcast, (const uint8_t *)msg,
                                 strlen(msg) + 1);
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
            ESP_LOGI(TAG, "TX -> broadcast: \"hola\"");
            hub_broadcast("hola");
            last_send = now;
        }
    }
}

#elif CONFIG_IDF_TARGET_ESP32C3
/* ------------------------------- node ----------------------------------- */

static char s_name[NEKO_NAME_LEN];

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

    ESP_LOGI(TAG, "node name: %s", s_name);
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

static void node_task(void *arg)
{
    (void)arg;
    rx_packet_t pkt;

    while (1) {
        if (xQueueReceive(s_rx_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char text[ESP_NOW_MAX_DATA_LEN + 1];
        int n = pkt.len < ESP_NOW_MAX_DATA_LEN ? pkt.len : ESP_NOW_MAX_DATA_LEN;
        memcpy(text, pkt.data, n);
        text[n] = '\0';

        /* Build "<received> mundo" and tag it with this node's name. */
        node_msg_t reply = { 0 };
        reply.magic = NEKO_MAGIC;
        copy_str(reply.name, sizeof(reply.name), s_name);

        const char suffix[] = " mundo";
        size_t tlen = strlen(text);
        if (tlen > sizeof(reply.text) - sizeof(suffix)) {
            tlen = sizeof(reply.text) - sizeof(suffix);
        }
        memcpy(reply.text, text, tlen);
        memcpy(reply.text + tlen, suffix, sizeof(suffix));

        if (!esp_now_is_peer_exist(pkt.src)) {
            esp_now_peer_info_t peer = { 0 };
            memcpy(peer.peer_addr, pkt.src, sizeof(peer.peer_addr));
            peer.channel = ESPNOW_CHANNEL;
            peer.ifidx = WIFI_IF_STA;
            peer.encrypt = false;

            esp_err_t err = esp_now_add_peer(&peer);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "could not add peer " MACSTR ": %s",
                         MAC2STR(pkt.src), esp_err_to_name(err));
                continue;
            }
        }

        ESP_LOGI(TAG, "RX <- " MACSTR ": \"%s\" -> TX: %s: \"%s\"",
                 MAC2STR(pkt.src), text, reply.name, reply.text);

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
