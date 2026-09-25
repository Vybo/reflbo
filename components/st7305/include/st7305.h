#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "st7305_frame.h"

/* The two vendor init sequences (AGENTS.md gotcha 6). They differ in the source high voltages
 * (contrast), the oscillator and the frame rates. */
typedef enum {
    ST7305_VARIANT_FACTORY, /* factory firmware: SPI 10 MHz, VSHP/VSHN 0x41, HPM 16 Hz, LPM 8 Hz */
    ST7305_VARIANT_XIAOZHI, /* XiaoZhi firmware: SPI 40 MHz, VSHP 0x69, VSHN 0x4B, HPM 25.5 Hz, LPM 1 Hz */
} st7305_variant_t;

typedef enum {
    ST7305_MODE_HPM, /* high power mode: fast refresh, for interaction */
    ST7305_MODE_LPM, /* low power mode: slow refresh, for idle */
} st7305_mode_t;

/* Cold start: SPI bus, hardware reset, init sequence. The panel RAM is undefined until the first push. */
esp_err_t st7305_init(st7305_variant_t variant);
/* Resets the panel and runs another init sequence (the SPI clock follows the variant). */
esp_err_t st7305_reinit(st7305_variant_t variant);
/* Converts and sends a canonical frame (spec §4.1); blocks until the DMA transfer has finished. */
esp_err_t st7305_push(const uint8_t *canonical);
esp_err_t st7305_set_mode(st7305_mode_t mode);
st7305_variant_t st7305_variant(void);
st7305_mode_t st7305_mode(void);
