#include "display.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "util_crc32.h"

static const char *TAG = "display";

static gfx_fb_t s_fb; /* s_fb.buf stays NULL until display_init */
static uint32_t s_last_crc;
static bool s_pushed;

static esp_err_t alloc_fb(void)
{
    ESP_RETURN_ON_FALSE(s_fb.buf == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    uint8_t *buf = heap_caps_calloc(1, ST7305_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    gfx_fb_init(&s_fb, buf, ST7305_WIDTH, ST7305_HEIGHT);
    return ESP_OK;
}

esp_err_t display_init_warm(const display_state_t *state)
{
    ESP_RETURN_ON_ERROR(alloc_fb(), TAG, "framebuffer");
    ESP_RETURN_ON_ERROR(st7305_init_warm(state->variant, state->mode, state->lpm_rate), TAG, "panel attach");
    s_last_crc = state->last_crc;
    s_pushed = state->pushed;
    return ESP_OK;
}

void display_export(display_state_t *out)
{
    *out = (display_state_t){
        .variant = st7305_variant(),
        .mode = st7305_mode(),
        .lpm_rate = st7305_lpm_rate(),
        .last_crc = s_last_crc,
        .pushed = s_pushed,
    };
}

esp_err_t display_prepare_deep_sleep(void)
{
    return st7305_prepare_deep_sleep();
}

esp_err_t display_init(st7305_variant_t variant)
{
    ESP_RETURN_ON_ERROR(alloc_fb(), TAG, "framebuffer");
    ESP_RETURN_ON_ERROR(st7305_init(variant), TAG, "panel init");
    int64_t start = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(display_commit(true), TAG, "first frame");
    ESP_LOGI(TAG, "first frame pushed in %lld us", (long long)(esp_timer_get_time() - start));
    return st7305_set_mode(ST7305_MODE_LPM);
}

gfx_fb_t *display_fb(void)
{
    return s_fb.buf != NULL ? &s_fb : NULL;
}

esp_err_t display_commit(bool force)
{
    ESP_RETURN_ON_FALSE(s_fb.buf != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    uint32_t crc = util_crc32(0, s_fb.buf, ST7305_FRAME_BYTES);
    if (!force && s_pushed && crc == s_last_crc) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(st7305_push(s_fb.buf), TAG, "push");
    s_last_crc = crc;
    s_pushed = true;
    return ESP_OK;
}

esp_err_t display_set_variant(st7305_variant_t variant)
{
    ESP_RETURN_ON_ERROR(st7305_reinit(variant), TAG, "reinit");
    ESP_RETURN_ON_ERROR(display_commit(true), TAG, "frame");
    return st7305_set_mode(ST7305_MODE_LPM);
}
