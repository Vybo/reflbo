#pragma once

/* Waveshare ESP32-S3-RLCD-4.2 pin map (AGENTS.md §3.2, checked against the schematic). */

#define BOARD_PIN_BOOT     0  /* BOOT button: active low, external 10k pull-up and 100 nF */
#define BOARD_PIN_BAT_ADC  4  /* ADC1_CH3 = VBAT x 1/3 */
#define BOARD_PIN_LCD_DC   5
#define BOARD_PIN_LCD_TE   6
#define BOARD_PIN_LCD_SCK  11
#define BOARD_PIN_LCD_MOSI 12
#define BOARD_PIN_I2C_SDA  13 /* external 2.2k pull-ups */
#define BOARD_PIN_I2C_SCL  14
#define BOARD_PIN_RTC_INT  15 /* PCF85063 INT: open drain, active low, no external pull-up */
#define BOARD_PIN_KEY      18 /* KEY button: active low, external 10k pull-up, no capacitor */
#define BOARD_PIN_LCD_CS   40 /* digital-only pad */
#define BOARD_PIN_LCD_RST  41 /* digital-only pad; low resets the panel */
#define BOARD_PIN_PA_CTRL  46 /* speaker amp enable: external 10k pull-down */
