#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "gfx.h"
#include "st7305.h"

/* Display service (spec §4.1): owns the canonical framebuffer (in PSRAM) and pushes it to the panel.
 *
 * Not thread-safe. The display and st7305 belong to the app task (spec §3.2). Until that task exists
 * (M2), app_main uses them during boot and the console task afterwards, never both at once. From
 * M2, console commands that draw or push must go through the app task. */
esp_err_t display_init(st7305_variant_t variant); /* cold start: white frame, then LPM */
gfx_fb_t *display_fb(void); /* NULL until display_init has allocated the framebuffer */
/* Pushes the framebuffer if it changed since the last push (CRC32), or always when force is set. */
esp_err_t display_commit(bool force);
/* Re-initialises the panel with another init sequence and pushes the current frame. */
esp_err_t display_set_variant(st7305_variant_t variant);
