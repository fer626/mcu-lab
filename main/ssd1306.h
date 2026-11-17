/*
 * ssd1306.h - minimal SSD1306/SH1106 128x64 I2C OLED driver.
 *
 * No external component is required: it talks to the panel through the
 * ESP-IDF I2C master driver and renders a 5x7 ASCII font (6 px cell).
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

/* Bring up the I2C bus and initialise the panel. Returns ESP_OK on success. */
esp_err_t ssd1306_init(void);

/* Clear the local frame buffer (does not touch the panel until flush). */
void ssd1306_clear(void);

/* Draw NUL-terminated ASCII text. `y` must be a multiple of 8. */
void ssd1306_draw_text(int x, int y, const char *text);

/* Push the local frame buffer to the panel. */
void ssd1306_flush(void);
