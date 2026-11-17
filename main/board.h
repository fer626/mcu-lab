/*
 * board.h - pin map and tunables for the ESP-NOW demo.
 *
 * Defaults target an ESP32-S3-DevKitC-1 (N16R8) with an SSD1306/SH1106
 * 128x64 I2C OLED wired to the I2C bus below. Change the GPIOs if your
 * wiring differs. GPIO8/GPIO9 are free on the DevKitC-1 and are not used
 * by the flash/PSRAM, the USB port or the UART bridge.
 *
 * OLED module -> ESP32-S3
 *   VCC -> 3V3
 *   GND -> GND
 *   SDA -> GPIO8
 *   SCL -> GPIO9
 */
#pragma once

/* ---- OLED (I2C, SSD1306/SH1106 128x64) ---- */
#define OLED_SDA_GPIO   8
#define OLED_SCL_GPIO   9
#define OLED_I2C_ADDR   0x3C   /* most modules; some use 0x3D */
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

/* ---- Radio tunables (heat vs. range) ---- */
#define ESPNOW_CHANNEL   1
/* The original demo sent every 2 s. A slower period means the node's radio
 * wakes up (and answers) far less often, which is the main source of heat. */
#define SEND_PERIOD_MS   10000
/* TX power in 0.25 dBm steps: 34 * 0.25 = 8.5 dBm (chip default is 20 dBm). */
#define TX_POWER_QDBM    34
