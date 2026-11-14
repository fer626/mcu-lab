/*
 * app - minimal ESP-IDF firmware
 *
 * Prints chip information and a periodic heartbeat on the serial console.
 * Meant as a starting point: build it in CI, flash it, and read the logs.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"

static const char *TAG = "app";

void app_main(void)
{
    /* Initialize NVS. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  boot");
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  target      : %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "  cores       : %d", chip.cores);
    ESP_LOGI(TAG, "  revision    : %d", chip.revision);
    ESP_LOGI(TAG, "  MAC (STA)   : %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "  idf         : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "  free heap   : %u bytes", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "==============================================");

    int n = 0;
    while (1) {
        ESP_LOGI(TAG, "tick #%d (uptime %lld s)",
                 n++, (long long)(esp_timer_get_time() / 1000000));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
