#include "board.h"
#include "board_buttons.h"
#include "diag.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gfx_test_pattern.h"

static const char *TAG = "main";

/* The owner's pick at the M1 panel check (2026-09-25): the factory sequence has the better contrast.
 * The LPM refresh rate stays at the st7305 default of 1 Hz, which showed no contrast loss. */
#define DISPLAY_VARIANT ST7305_VARIANT_FACTORY

/* Dashboard bindings (spec §5.6): KEY double toggles auto-cycle, BOOT long needs 3 s. */
static const gesture_config_t k_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = true },
    [BOARD_BUTTON_BOOT] = { .long_ms = 3000, .double_enabled = false },
};

static void on_button(board_button_t button, gesture_t gesture)
{
    (void)button;
    (void)gesture; /* the buttons task logs each gesture; the app task binds them (M2 Task 12) */
}

void app_main(void)
{
    ESP_LOGI(TAG, "reflbo %s starting", esp_app_get_description()->version);

    esp_err_t err = board_init(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(err));
    }

    err = display_init(DISPLAY_VARIANT);
    if (err == ESP_OK) {
        gfx_draw_test_pattern(display_fb());
        err = display_commit(false);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display failed: %s", esp_err_to_name(err));
    }

    err = board_buttons_start(on_button, k_buttons);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buttons failed: %s", esp_err_to_name(err));
    }

    err = diag_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "diagnostics console failed to start: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "reflbo ready");
}
