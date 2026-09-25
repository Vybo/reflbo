#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "st7305_frame.h"

/* The two vendor init sequences (AGENTS.md gotcha 6). They differ in the source high voltages
 * (contrast), the oscillator (HPM frame rate) and the LPM frame rate. The driver replaces the
 * sequence's LPM rate (factory 8 Hz, XiaoZhi 1 Hz) with st7305_lpm_rate(). */
typedef enum {
    ST7305_VARIANT_FACTORY, /* factory firmware: SPI 10 MHz, VSHP/VSHN 0x41, HPM 16 Hz */
    ST7305_VARIANT_XIAOZHI, /* XiaoZhi firmware: SPI 40 MHz, VSHP 0x69, VSHN 0x4B, HPM 25.5 Hz */
} st7305_variant_t;

typedef enum {
    ST7305_MODE_HPM, /* high power mode: fast refresh, for interaction */
    ST7305_MODE_LPM, /* low power mode: slow refresh, for idle */
} st7305_mode_t;

/* LPM refresh rate. The values are the LFRA codes of FRCTRL (B2h), datasheet §8.2.3. Idle stays in
 * LPM, so this sets the panel's idle power draw. */
typedef enum {
    ST7305_LPM_0_25HZ,
    ST7305_LPM_0_5HZ,
    ST7305_LPM_1HZ, /* default (spec §4.2) */
    ST7305_LPM_2HZ,
    ST7305_LPM_4HZ,
    ST7305_LPM_8HZ,
} st7305_lpm_rate_t;

/* Cold start: SPI bus, hardware reset, init sequence. The panel RAM is undefined until the first push. */
esp_err_t st7305_init(st7305_variant_t variant);
/* Resets the panel and runs another init sequence (the SPI clock follows the variant). */
esp_err_t st7305_reinit(st7305_variant_t variant);
/* Converts and sends a canonical frame (spec §4.1); blocks until the DMA transfer has finished. */
esp_err_t st7305_push(const uint8_t *canonical);
/* Switches the power mode with the datasheet §7.11 sequence: about 120 ms into LPM, 320 ms into HPM. */
esp_err_t st7305_set_mode(st7305_mode_t mode);
/* Sets the LPM refresh rate. Before st7305_init it is only stored; every init applies it. */
esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate);
st7305_variant_t st7305_variant(void);
st7305_mode_t st7305_mode(void);
st7305_lpm_rate_t st7305_lpm_rate(void);
const char *st7305_lpm_rate_name(st7305_lpm_rate_t rate); /* "0.25" ... "8" (Hz) */
/* Counts the panel's tearing-effect pulses (TE, GPIO6: one per panel frame) for window_ms. */
esp_err_t st7305_count_frames(uint32_t window_ms, uint32_t *frames);
