#include "st7305.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define PIN_MOSI    12
#define PIN_SCLK    11
#define PIN_DC      5
#define PIN_CS      40
#define PIN_RST     41
#define PIN_TE      6
#define SPI_HOST_ID SPI3_HOST

static const char *TAG = "st7305";

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[10];
    uint16_t delay_ms;
} init_cmd_t;

/* From ref/waveshare: 10_FactoryProgram port_bsp/display_bsp.cpp and the XiaoZhi board's
 * custom_lcd_display.cc. Identical except C1, C4, D8 and B2. */
static const init_cmd_t s_init_factory[] = {
    { 0xD6, 2, { 0x17, 0x02 }, 0 },                  /* NVMLOADCTRL */
    { 0xD1, 1, { 0x01 }, 0 },                        /* BSTEN: booster on */
    { 0xC0, 2, { 0x11, 0x04 }, 0 },                  /* GCTRL: gate voltages */
    { 0xC1, 4, { 0x41, 0x41, 0x41, 0x41 }, 0 },      /* VSHPCTRL: source high, positive */
    { 0xC2, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },      /* VSLPCTRL: source low, positive */
    { 0xC4, 4, { 0x41, 0x41, 0x41, 0x41 }, 0 },      /* VSHNCTRL: source high, negative */
    { 0xC5, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },      /* VSLNCTRL: source low, negative */
    { 0xD8, 2, { 0xA6, 0xE9 }, 0 },                  /* OSCSET */
    { 0xB2, 1, { 0x05 }, 0 },                        /* FRCTRL: HPM 16 Hz, LPM 8 Hz */
    { 0xB3, 10, { 0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 }, /* GTUPEQH */
    { 0xB4, 8, { 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 },             /* GTUPEQL */
    { 0x62, 3, { 0x32, 0x03, 0x1F }, 0 },
    { 0xB7, 1, { 0x13 }, 0 },                        /* SOUEQ */
    { 0xB0, 1, { 0x64 }, 0 },                        /* GATESET */
    { 0x11, 0, { 0 }, 200 },                         /* SLPOUT */
    { 0xC9, 1, { 0x00 }, 0 },                        /* VSHLSEL */
    { 0x36, 1, { 0x48 }, 0 },                        /* MADCTL */
    { 0x3A, 1, { 0x11 }, 0 },                        /* DTFORM */
    { 0xB9, 1, { 0x20 }, 0 },                        /* GAMAMS: mono */
    { 0xB8, 1, { 0x29 }, 0 },                        /* PNLSET */
    { 0x21, 0, { 0 }, 0 },                           /* INVON */
    { 0x2A, 2, { 0x12, 0x2A }, 0 },                  /* CASET */
    { 0x2B, 2, { 0x00, 0xC7 }, 0 },                  /* RASET */
    { 0x35, 1, { 0x00 }, 0 },                        /* TEON */
    { 0xD0, 1, { 0xFF }, 0 },                        /* AUTOPWRCTRL */
    { 0x38, 0, { 0 }, 0 },                           /* HPM */
    { 0x29, 0, { 0 }, 0 },                           /* DISPON */
};

static const init_cmd_t s_init_xiaozhi[] = {
    { 0xD6, 2, { 0x17, 0x02 }, 0 },
    { 0xD1, 1, { 0x01 }, 0 },
    { 0xC0, 2, { 0x11, 0x04 }, 0 },
    { 0xC1, 4, { 0x69, 0x69, 0x69, 0x69 }, 0 },      /* VSHPCTRL */
    { 0xC2, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },
    { 0xC4, 4, { 0x4B, 0x4B, 0x4B, 0x4B }, 0 },      /* VSHNCTRL */
    { 0xC5, 4, { 0x19, 0x19, 0x19, 0x19 }, 0 },
    { 0xD8, 2, { 0x80, 0xE9 }, 0 },                  /* OSCSET */
    { 0xB2, 1, { 0x02 }, 0 },                        /* FRCTRL: HPM 25.5 Hz, LPM 1 Hz */
    { 0xB3, 10, { 0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 },
    { 0xB4, 8, { 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45 }, 0 },
    { 0x62, 3, { 0x32, 0x03, 0x1F }, 0 },
    { 0xB7, 1, { 0x13 }, 0 },
    { 0xB0, 1, { 0x64 }, 0 },
    { 0x11, 0, { 0 }, 200 },
    { 0xC9, 1, { 0x00 }, 0 },
    { 0x36, 1, { 0x48 }, 0 },
    { 0x3A, 1, { 0x11 }, 0 },
    { 0xB9, 1, { 0x20 }, 0 },
    { 0xB8, 1, { 0x29 }, 0 },
    { 0x21, 0, { 0 }, 0 },
    { 0x2A, 2, { 0x12, 0x2A }, 0 },
    { 0x2B, 2, { 0x00, 0xC7 }, 0 },
    { 0x35, 1, { 0x00 }, 0 },
    { 0xD0, 1, { 0xFF }, 0 },
    { 0x38, 0, { 0 }, 0 },
    { 0x29, 0, { 0 }, 0 },
};

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;
static uint8_t *s_panel; /* DMA-capable internal RAM */
static bool s_bus_ready;
static st7305_variant_t s_variant;
static st7305_mode_t s_mode;
static st7305_lpm_rate_t s_lpm_rate = ST7305_LPM_1HZ;
static volatile uint32_t s_te_pulses;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t create_io(uint32_t pclk_hz)
{
    if (s_io != NULL) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(s_io), TAG, "panel IO delete failed");
        s_io = NULL;
    }
    esp_lcd_panel_io_spi_config_t cfg = {
        .cs_gpio_num = PIN_CS,
        .dc_gpio_num = PIN_DC,
        .spi_mode = 0,
        .pclk_hz = pclk_hz,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST_ID, &cfg, &s_io);
}

static void hardware_reset(void)
{
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t run_init(const init_cmd_t *cmds, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, cmds[i].cmd, cmds[i].len ? cmds[i].data : NULL,
                                                      cmds[i].len),
                            TAG, "init command 0x%02X failed", cmds[i].cmd);
        if (cmds[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(cmds[i].delay_ms));
        }
    }
    return ESP_OK;
}

/* FRCTRL (B2h) = HFRA << 4 | LFRA. HFRA stays 0, as in both vendor sequences: HPM 16 Hz with the
 * factory oscillator setting, 25.5 Hz with XiaoZhi's. */
static esp_err_t write_frame_rate(void)
{
    const uint8_t frctrl = (uint8_t)s_lpm_rate;
    return esp_lcd_panel_io_tx_param(s_io, 0xB2, &frctrl, 1);
}

esp_err_t st7305_init(st7305_variant_t variant)
{
    ESP_RETURN_ON_FALSE(!s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    s_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_done != NULL, ESP_ERR_NO_MEM, TAG, "semaphore");
    s_panel = heap_caps_malloc(ST7305_FRAME_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_NO_MEM, TAG, "panel buffer");

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7305_FRAME_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI_HOST_ID, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "reset pin");
    s_bus_ready = true;
    return st7305_reinit(variant);
}

esp_err_t st7305_reinit(st7305_variant_t variant)
{
    ESP_RETURN_ON_FALSE(s_bus_ready, ESP_ERR_INVALID_STATE, TAG, "call st7305_init first");
    const bool xiaozhi = variant == ST7305_VARIANT_XIAOZHI;
    ESP_RETURN_ON_ERROR(create_io(xiaozhi ? 40 * 1000 * 1000 : 10 * 1000 * 1000), TAG, "panel IO");
    hardware_reset();
    if (xiaozhi) {
        ESP_RETURN_ON_ERROR(run_init(s_init_xiaozhi, sizeof(s_init_xiaozhi) / sizeof(s_init_xiaozhi[0])), TAG,
                            "XiaoZhi init");
    } else {
        ESP_RETURN_ON_ERROR(run_init(s_init_factory, sizeof(s_init_factory) / sizeof(s_init_factory[0])), TAG,
                            "factory init");
    }
    ESP_RETURN_ON_ERROR(write_frame_rate(), TAG, "frame rate");
    s_variant = variant;
    s_mode = ST7305_MODE_HPM;
    ESP_LOGI(TAG, "%s init sequence, SPI %d MHz, LPM %s Hz", xiaozhi ? "XiaoZhi" : "factory", xiaozhi ? 40 : 10,
             st7305_lpm_rate_name(s_lpm_rate));
    return ESP_OK;
}

esp_err_t st7305_push(const uint8_t *canonical)
{
    static const uint8_t columns[] = { 0x12, 0x2A };
    static const uint8_t rows[] = { 0x00, 0xC7 };

    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    st7305_frame_to_panel(canonical, s_panel);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x2A, columns, sizeof(columns)), TAG, "column window");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x2B, rows, sizeof(rows)), TAG, "row window");
    xSemaphoreTake(s_done, 0); /* drop a stale completion */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_io, 0x2C, s_panel, ST7305_FRAME_BYTES), TAG, "frame");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) == pdTRUE, ESP_ERR_TIMEOUT, TAG,
                        "frame transfer timed out");
    return ESP_OK;
}

/* Datasheet §7.11 sets the source voltages for the new mode during a switch. Both vendor sequences
 * load the same values into all four voltage sets of C1h/C2h/C4h/C5h, so that step reselects set 1. */
static esp_err_t select_source_voltages(void)
{
    const uint8_t set = 0x00;
    return esp_lcd_panel_io_tx_param(s_io, 0xC9, &set, 1);
}

esp_err_t st7305_set_mode(st7305_mode_t mode)
{
    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    if (mode == s_mode) {
        return ESP_OK;
    }
    if (mode == ST7305_MODE_LPM) { /* HPM => LPM */
        ESP_RETURN_ON_ERROR(select_source_voltages(), TAG, "LPM voltages");
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x39, NULL, 0), TAG, "LPM");
        vTaskDelay(pdMS_TO_TICKS(100));
    } else { /* LPM => HPM */
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x38, NULL, 0), TAG, "HPM");
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_RETURN_ON_ERROR(select_source_voltages(), TAG, "HPM voltages");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_mode = mode;
    return ESP_OK;
}

esp_err_t st7305_set_lpm_rate(st7305_lpm_rate_t rate)
{
    ESP_RETURN_ON_FALSE((unsigned)rate <= ST7305_LPM_8HZ, ESP_ERR_INVALID_ARG, TAG, "LPM rate");
    s_lpm_rate = rate;
    if (s_io == NULL) {
        return ESP_OK; /* st7305_init applies it */
    }
    return write_frame_rate(); /* applies at once, also in LPM (measured with panel fps) */
}

st7305_variant_t st7305_variant(void)
{
    return s_variant;
}

st7305_mode_t st7305_mode(void)
{
    return s_mode;
}

st7305_lpm_rate_t st7305_lpm_rate(void)
{
    return s_lpm_rate;
}

static void IRAM_ATTR on_te(void *arg)
{
    (void)arg;
    s_te_pulses++;
}

esp_err_t st7305_count_frames(uint32_t window_ms, uint32_t *frames)
{
    ESP_RETURN_ON_FALSE(s_io != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    gpio_config_t te = {
        .pin_bit_mask = 1ULL << PIN_TE,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te), TAG, "TE pin");
    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "GPIO ISR service");
    s_te_pulses = 0;
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(PIN_TE, on_te, NULL), TAG, "TE handler");
    vTaskDelay(pdMS_TO_TICKS(window_ms));
    gpio_isr_handler_remove(PIN_TE);
    gpio_set_intr_type(PIN_TE, GPIO_INTR_DISABLE);
    *frames = s_te_pulses;
    return ESP_OK;
}

const char *st7305_lpm_rate_name(st7305_lpm_rate_t rate)
{
    static const char *const names[] = { "0.25", "0.5", "1", "2", "4", "8" };
    return (unsigned)rate < sizeof(names) / sizeof(names[0]) ? names[rate] : "?";
}
