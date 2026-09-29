#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "gfx.h"
#include "st7305.h"

/* Display service (spec §4.1): owns the canonical framebuffer (in PSRAM) and pushes it to the panel.
 *
 * Not thread-safe: the display and st7305 belong to the app task (spec §3.2). Console commands
 * that draw or push run there through the diag executor. */

/* What must survive deep sleep to re-attach the panel without a reset (app RTC-RAM snapshot). */
typedef struct {
    st7305_variant_t variant;
    st7305_mode_t mode;
    st7305_lpm_rate_t lpm_rate;
    uint32_t last_crc; /* CRC of the frame the panel shows */
    bool pushed;
    bool asleep; /* sleep-in: night sleep (spec §9.1) */
} display_state_t;

esp_err_t display_init(st7305_variant_t variant); /* cold start: white frame, then LPM */
/* Deep-sleep wake: the panel still shows the last frame; attach without reset or clear. */
esp_err_t display_init_warm(const display_state_t *state);
void display_export(display_state_t *out);
/* Holds the panel pins for deep sleep. The last display call before sleeping. */
esp_err_t display_prepare_deep_sleep(void);
void display_cancel_deep_sleep(void); /* after display_prepare_deep_sleep() failed */
gfx_fb_t *display_fb(void); /* NULL until display_init has allocated the framebuffer */
/* Night sleep (spec §9.1): the panel stops scanning and its image fades; waking pushes the frame
 * again and returns the panel to LPM. */
esp_err_t display_sleep(void);
esp_err_t display_wake(void);
bool display_asleep(void);
/* Pushes the framebuffer if it changed since the last push (CRC32), or always when force is set. */
esp_err_t display_commit(bool force);
/* Re-initialises the panel with another init sequence and pushes the current frame. */
esp_err_t display_set_variant(st7305_variant_t variant);
