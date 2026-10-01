#include "pcf85063.h"

#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pcf85063_regs.h"

#define I2C_TIMEOUT_MS  50 /* at least two 10 ms ticks; shorter timeouts round down to zero */
#define OS_CLEAR_TRIES  20 /* the oscillator can take up to 2 s to start (datasheet §8.3.1.1) */
#define POLL_US         500 /* the RTC second's start is timed to about this */

static const char *TAG = "pcf85063";

static i2c_master_dev_handle_t s_dev;

static esp_err_t write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + PCF85063_TIME_LEN];
    ESP_RETURN_ON_FALSE(len <= PCF85063_TIME_LEN, ESP_ERR_INVALID_SIZE, TAG, "write too long");
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) {
        buf[1 + i] = data[i];
    }
    return i2c_master_transmit(s_dev, buf, len + 1, I2C_TIMEOUT_MS);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
}

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus)
{
    if (s_dev == NULL) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PCF85063_ADDR,
            .scl_speed_hz = 400000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");
    }
    const uint8_t control[] = { PCF85063_CONTROL_1_RUN, PCF85063_CONTROL_2_RUN };
    ESP_RETURN_ON_ERROR(write_regs(PCF85063_REG_CONTROL_1, control, sizeof(control)), TAG, "control");
    const uint8_t timer = PCF85063_TIMER_MODE_OFF;
    return write_regs(PCF85063_REG_TIMER_MODE, &timer, 1);
}

esp_err_t pcf85063_read(time_t *utc, bool *valid)
{
    uint8_t regs[PCF85063_TIME_LEN];
    ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_SECONDS, regs, sizeof(regs)), TAG, "read time");
    bool stopped;
    ESP_RETURN_ON_FALSE(pcf85063_decode_time(regs, utc, &stopped), ESP_ERR_INVALID_RESPONSE, TAG,
                        "impossible time %02x %02x %02x %02x", regs[0], regs[1], regs[2], regs[3]);
    *valid = !stopped;
    return ESP_OK;
}

esp_err_t pcf85063_write(time_t utc)
{
    for (int attempt = 0; attempt < OS_CLEAR_TRIES; attempt++) {
        uint8_t regs[PCF85063_TIME_LEN];
        pcf85063_encode_time(utc, regs);
        ESP_RETURN_ON_ERROR(write_regs(PCF85063_REG_SECONDS, regs, sizeof(regs)), TAG, "write time");
        uint8_t seconds;
        ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_SECONDS, &seconds, 1), TAG, "read back");
        if ((seconds & 0x80) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); /* oscillator not stable yet (only after power-on): retry */
    }
    ESP_LOGE(TAG, "oscillator-stop flag stays set: the RTC oscillator is not running");
    return ESP_ERR_INVALID_STATE;
}

esp_err_t pcf85063_set_alarm(time_t wake)
{
    uint8_t regs[PCF85063_ALARM_LEN];
    pcf85063_encode_alarm(wake, regs);
    ESP_RETURN_ON_ERROR(write_regs(PCF85063_REG_ALARM, regs, sizeof(regs)), TAG, "alarm");
    return pcf85063_clear_alarm();
}

esp_err_t pcf85063_clear_alarm(void)
{
    const uint8_t control_2 = PCF85063_CONTROL_2_RUN; /* AF = 0 clears it; the other flags stay */
    return write_regs(PCF85063_REG_CONTROL_2, &control_2, 1);
}

static int64_t system_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* Waits until the system clock reads `at_us`: sleeps in ticks, then spins the last stretch. */
static void wait_until(int64_t at_us)
{
    int64_t left = at_us - system_us();
    if (left > 30000) {
        vTaskDelay(pdMS_TO_TICKS((left - 20000) / 1000));
    }
    while (system_us() < at_us) {
        esp_rom_delay_us(50);
    }
}

esp_err_t pcf85063_write_precise(int64_t *set_at_ms)
{
    /* the second after next: room for the writes, and the release comes 0.5079 s before it */
    int64_t now_us = system_us();
    int64_t target_s = now_us / 1000000 + 2;
    if (target_s * 1000000 - now_us > 1600000) {
        target_s--;
    }
    const uint8_t stop = PCF85063_CONTROL_1_RUN | PCF85063_CONTROL_1_STOP;
    const uint8_t run = PCF85063_CONTROL_1_RUN;
    ESP_RETURN_ON_ERROR(write_regs(PCF85063_REG_CONTROL_1, &stop, 1), TAG, "stop");
    uint8_t regs[PCF85063_TIME_LEN];
    pcf85063_encode_time((time_t)(target_s - 1), regs); /* ticks to target_s at the first increment */
    esp_err_t err = write_regs(PCF85063_REG_SECONDS, regs, sizeof(regs));
    wait_until(target_s * 1000000 - PCF85063_STOP_FIRST_TICK_US);
    esp_err_t run_err = write_regs(PCF85063_REG_CONTROL_1, &run, 1); /* released even if the write failed */
    ESP_RETURN_ON_ERROR(err, TAG, "write time");
    ESP_RETURN_ON_ERROR(run_err, TAG, "release");
    uint8_t seconds;
    ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_SECONDS, &seconds, 1), TAG, "read back");
    if (seconds & 0x80) {
        return pcf85063_write((time_t)target_s); /* the oscillator isn't running yet: the plain way */
    }
    if (set_at_ms != NULL) {
        *set_at_ms = target_s * 1000;
    }
    return ESP_OK;
}

esp_err_t pcf85063_error_ms(int64_t *error_ms)
{
    uint8_t first, now;
    ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_SECONDS, &first, 1), TAG, "read seconds");
    int64_t give_up = system_us() + 1100000;
    do {
        esp_rom_delay_us(POLL_US);
        ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_SECONDS, &now, 1), TAG, "read seconds");
    } while (now == first && system_us() < give_up);
    int64_t edge_us = system_us() - POLL_US / 2; /* the second began within the last poll */
    ESP_RETURN_ON_FALSE(now != first, ESP_ERR_TIMEOUT, TAG, "the RTC's seconds don't tick");
    time_t rtc;
    bool valid;
    ESP_RETURN_ON_ERROR(pcf85063_read(&rtc, &valid), TAG, "read time");
    ESP_RETURN_ON_FALSE(valid, ESP_ERR_INVALID_STATE, TAG, "the oscillator stopped");
    *error_ms = (int64_t)rtc * 1000 - edge_us / 1000;
    return ESP_OK;
}

esp_err_t pcf85063_set_offset(int steps)
{
    const uint8_t reg = pcf85063_encode_offset(steps);
    return write_regs(PCF85063_REG_OFFSET, &reg, 1);
}

esp_err_t pcf85063_get_offset(int *steps)
{
    uint8_t reg;
    ESP_RETURN_ON_ERROR(read_regs(PCF85063_REG_OFFSET, &reg, 1), TAG, "offset");
    *steps = pcf85063_decode_offset(reg);
    return ESP_OK;
}
