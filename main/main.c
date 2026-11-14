/*
 * app - ESP-NOW "hello" demo
 *
 * ESP32-S3 (hub)  : broadcasts "hola" every 2 seconds.
 * ESP32-C3 (node) : replies "<received> mundo" to the sender, so the hub
 *                   receives "hola mundo".
 *
 * No Wi-Fi AP, router or credentials are needed. Both boards use ESP-NOW
 * on a fixed channel. The hub talks to the broadcast address, so no MAC
 * has to be hard-coded; the node answers to the source address it sees.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define ESPNOW_CHANNEL   1
#define SEND_PERIOD_MS   2000
#define RX_QUEUE_LEN     4

static const char *TAG = "app";

typedef struct {
    uint8_t src[6];
    int len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} rx_packet_t;

static QueueHandle_t s_rx_queue;

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
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

#if CONFIG_IDF_TARGET_ESP32S3
/* ------------------------------- hub ------------------------------------ */

static const uint8_t s_broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void hub_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    ESP_LOGI(TAG, "TX -> broadcast: %s",
             status == ESP_NOW_SEND_SUCCESS ? "ok" : "failed");
}

static void hub_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    char msg[ESP_NOW_MAX_DATA_LEN + 1];
    int n = len < ESP_NOW_MAX_DATA_LEN ? len : ESP_NOW_MAX_DATA_LEN;
    memcpy(msg, data, n);
    msg[n] = '\0';

    ESP_LOGI(TAG, "RX <- " MACSTR ": \"%s\"", MAC2STR(info->src_addr), msg);
}

static void hub_task(void *arg)
{
    (void)arg;
    const char *msg = "hola";

    while (1) {
        ESP_LOGI(TAG, "TX -> broadcast: \"%s\"", msg);
        esp_err_t err = esp_now_send(s_broadcast, (const uint8_t *)msg,
                                     strlen(msg) + 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "send failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(SEND_PERIOD_MS));
    }
}

#elif CONFIG_IDF_TARGET_ESP32C3
/* ------------------------------- node ----------------------------------- */

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
        memcpy(text, pkt.data, pkt.len);
        text[pkt.len] = '\0';

        /* Build "<received> mundo" without exceeding ESP-NOW's 250-byte payload. */
        const char suffix[] = " mundo";
        char reply[ESP_NOW_MAX_DATA_LEN];
        size_t n = strlen(text);
        if (n > sizeof(reply) - sizeof(suffix)) {
            n = sizeof(reply) - sizeof(suffix);
        }
        memcpy(reply, text, n);
        memcpy(reply + n, suffix, sizeof(suffix));

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

        ESP_LOGI(TAG, "RX <- " MACSTR ": \"%s\" -> TX: \"%s\"",
                 MAC2STR(pkt.src), text, reply);

        esp_err_t err = esp_now_send(pkt.src, (const uint8_t *)reply,
                                     strlen(reply) + 1);
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
    xTaskCreate(hub_task, "hub", 4096, NULL, 5, NULL);
#elif CONFIG_IDF_TARGET_ESP32C3
    xTaskCreate(node_task, "node", 4096, NULL, 5, NULL);
#endif
}
