#include "sensors.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "shtc3_codec.h"
#include "util_ticks.h"

#define SHTC3_ADDR          0x70
#define SHTC3_CMD_WAKEUP    0x3517
#define SHTC3_CMD_SLEEP     0xB098
#define SHTC3_CMD_READ_ID   0xEFC8
#define SHTC3_CMD_MEASURE   0x7866 /* normal mode, T first, no clock stretching */
#define SHTC3_WAKEUP_US     300    /* datasheet: 240 µs max */
#define SHTC3_MEASURE_MS    13     /* datasheet: 12.1 ms max in normal mode */
#define I2C_TIMEOUT_MS      50     /* at least two 10 ms ticks; shorter timeouts round down to zero */

#define BAT_CHANNEL         ADC_CHANNEL_3 /* GPIO4 */
#define BAT_SAMPLES         16
#define BAT_DIVIDER         3             /* 200k / 100k */

static const char *TAG = "sensors";

static i2c_master_dev_handle_t s_shtc3;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static sensors_state_t s_state;

static esp_err_t shtc3_command(uint16_t cmd)
{
    const uint8_t bytes[2] = { (uint8_t)(cmd >> 8), (uint8_t)cmd };
    return i2c_master_transmit(s_shtc3, bytes, sizeof(bytes), I2C_TIMEOUT_MS);
}

static esp_err_t shtc3_wake(void)
{
    esp_err_t err = shtc3_command(SHTC3_CMD_WAKEUP);
    esp_rom_delay_us(SHTC3_WAKEUP_US);
    return err;
}

static esp_err_t shtc3_check_id(void)
{
    ESP_RETURN_ON_ERROR(shtc3_wake(), TAG, "wakeup");
    uint8_t id[3];
    esp_err_t err = shtc3_command(SHTC3_CMD_READ_ID);
    if (err == ESP_OK) {
        err = i2c_master_receive(s_shtc3, id, sizeof(id), I2C_TIMEOUT_MS);
    }
    shtc3_command(SHTC3_CMD_SLEEP);
    ESP_RETURN_ON_ERROR(err, TAG, "read id");
    ESP_RETURN_ON_FALSE(shtc3_crc8(id, 2) == id[2], ESP_ERR_INVALID_CRC, TAG, "id CRC");
    uint16_t value = (uint16_t)((id[0] << 8) | id[1]);
    ESP_RETURN_ON_FALSE((value & 0x083F) == 0x0807, ESP_ERR_NOT_FOUND, TAG, "unexpected id 0x%04x", value);
    return ESP_OK;
}

static esp_err_t adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "ADC unit");
    adc_oneshot_chan_cfg_t chan = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BAT_CHANNEL, &chan), TAG, "ADC channel");
    adc_cali_curve_fitting_config_t cali = {
        .unit_id = ADC_UNIT_1,
        .chan = BAT_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration in eFuse; battery readings are approximate");
        s_cali = NULL;
    }
    return ESP_OK;
}

esp_err_t sensors_init(i2c_master_bus_handle_t bus, bool cold)
{
    if (s_shtc3 == NULL) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SHTC3_ADDR,
            .scl_speed_hz = 400000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_shtc3), TAG, "add SHTC3");
    }
    if (cold) {
        battery_gauge_init(&s_state.gauge);
        esp_err_t err = shtc3_check_id();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SHTC3 not ready: %s", esp_err_to_name(err));
        }
    }
    return adc_init();
}

static esp_err_t read_env_once(int *temp_c100, int *hum_pct100)
{
    ESP_RETURN_ON_ERROR(shtc3_wake(), TAG, "wakeup");
    esp_err_t err = shtc3_command(SHTC3_CMD_MEASURE);
    uint8_t raw[6];
    if (err == ESP_OK) {
        vTaskDelay(util_ticks_at_least(SHTC3_MEASURE_MS, portTICK_PERIOD_MS));
        err = i2c_master_receive(s_shtc3, raw, sizeof(raw), I2C_TIMEOUT_MS);
    }
    shtc3_command(SHTC3_CMD_SLEEP); /* 45 µA idle otherwise */
    ESP_RETURN_ON_ERROR(err, TAG, "measure");
    ESP_RETURN_ON_FALSE(shtc3_parse(raw, temp_c100, hum_pct100), ESP_ERR_INVALID_CRC, TAG, "CRC");
    return ESP_OK;
}

esp_err_t sensors_sample_env(time_t now)
{
    int t, h;
    esp_err_t err = read_env_once(&t, &h);
    if (err != ESP_OK) {
        err = read_env_once(&t, &h); /* spec §16: retry once */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "SHTC3");
    s_state.env = (sensors_env_t){
        .valid = true,
        .temp_c100 = t + CONFIG_REFLBO_TEMP_OFFSET_C10 * 10,
        .hum_pct100 = h + CONFIG_REFLBO_HUM_OFFSET_PCT10 * 10,
        .time = now,
    };
    return ESP_OK;
}

esp_err_t sensors_sample_battery(time_t now)
{
    int raw, sum = 0, count = 0;
    adc_oneshot_read(s_adc, BAT_CHANNEL, &raw); /* the first sample after a pause reads low */
    for (int i = 0; i < BAT_SAMPLES; i++) {
        if (adc_oneshot_read(s_adc, BAT_CHANNEL, &raw) != ESP_OK) {
            continue;
        }
        int mv = raw * 3100 / 4095; /* rough 12 dB range without calibration */
        if (s_cali != NULL) {
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
        }
        sum += mv;
        count++;
    }
    ESP_RETURN_ON_FALSE(count > 0, ESP_FAIL, TAG, "ADC read");
    int mv = sum / count * BAT_DIVIDER * CONFIG_REFLBO_BATTERY_FACTOR_PERMILLE / 1000;
    s_state.last_mv = mv;
    s_state.battery_time = now;
    battery_gauge_add(&s_state.gauge, (uint32_t)now, mv);
    return ESP_OK;
}

sensors_env_t sensors_env(void)
{
    return s_state.env;
}

sensors_battery_t sensors_battery(time_t now)
{
    return (sensors_battery_t){
        .valid = battery_gauge_level(&s_state.gauge) >= 0,
        .last_mv = s_state.last_mv,
        .smoothed_mv = battery_gauge_mv(&s_state.gauge),
        .level = battery_gauge_level(&s_state.gauge),
        .state = battery_gauge_state(&s_state.gauge, (uint32_t)now),
        .time = s_state.battery_time,
    };
}

void sensors_export(sensors_state_t *out)
{
    *out = s_state;
}

void sensors_import(const sensors_state_t *in)
{
    s_state = *in;
}
