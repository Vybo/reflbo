#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diag_internal.h"
#include "esp_console.h"
#include "pcf85063.h"
#include "sensors.h"
#include "timekeeping.h"
#include "timekeeping_iso.h"

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

static void format_local(time_t t, char *out, size_t size)
{
    struct tm local;
    localtime_r(&t, &local);
    strftime(out, size, "%a %Y-%m-%d %H:%M:%S %Z", &local);
}

static int sensors_body(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = sensors_sample_env(time(NULL));
    if (err != ESP_OK) {
        printf("sensors: SHTC3 read failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    sensors_env_t env = sensors_env();
    int t = abs(env.temp_c100);
    printf("sensors: %s%d.%02d C, %d.%02d %%RH\n", env.temp_c100 < 0 ? "-" : "", t / 100, t % 100, env.hum_pct100 / 100,
           env.hum_pct100 % 100);
    return 0;
}

static const char *battery_state_name(battery_state_t state)
{
    switch (state) {
    case BATTERY_DISCHARGING:
        return "discharging";
    case BATTERY_CHARGING:
        return "charging";
    case BATTERY_FULL:
        return "full";
    default:
        return "unknown";
    }
}

static int battery_body(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "learn") == 0) { /* D21: from the next full discharge */
        if (argc != 3 || (strcmp(argv[2], "start") != 0 && strcmp(argv[2], "stop") != 0)) {
            printf("usage: battery learn start|stop\n");
            return 1;
        }
        if (strcmp(argv[2], "start") == 0) {
            sensors_learn_start();
        } else {
            sensors_learn_stop();
        }
    }
    time_t now = time(NULL);
    esp_err_t err = sensors_sample_battery(now);
    if (err != ESP_OK) {
        printf("battery: ADC read failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    sensors_battery_t b = sensors_battery(now);
    printf("battery: %d mV now, %d mV smoothed, %d %%, %s\n", b.last_mv, b.smoothed_mv, b.level,
           battery_state_name(b.state));
    const battery_learn_t *l = sensors_learn();
    printf("learning: %s, %u h of discharge, %d points\n", battery_learn_state_name(battery_learn_state(l)),
           (unsigned)battery_learn_hours(l), battery_learn_points(l));
    return 0;
}

static int rtc_body(int argc, char **argv)
{
    static const char *const k_usage = "rtc get | rtc set <YYYY-MM-DDTHH:MM[:SS][Z|+HH:MM]>";
    if (argc == 2 && strcmp(argv[1], "get") == 0) {
        time_t utc;
        bool valid;
        esp_err_t err = pcf85063_read(&utc, &valid);
        if (err != ESP_OK) {
            printf("rtc: %s\n", esp_err_to_name(err));
            return 1;
        }
        char iso[32], local[48];
        struct tm tm_utc;
        gmtime_r(&utc, &tm_utc);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        format_local(utc, local, sizeof(local));
        printf("rtc: %s (%s), local %s\n", iso, valid ? "valid" : "INVALID: oscillator stopped, set the time",
               local);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "set") == 0) {
        time_t utc;
        struct tm year_check;
        if (!timekeeping_parse_iso8601(argv[2], &utc) || gmtime_r(&utc, &year_check) == NULL ||
            year_check.tm_year < 100 || year_check.tm_year > 199) { /* the RTC holds 2000-2099 */
            return usage(k_usage);
        }
        esp_err_t err = timekeeping_set_utc(utc);
        if (err != ESP_OK) {
            printf("rtc: %s\n", esp_err_to_name(err));
            return 1;
        }
        char local[48];
        format_local(utc, local, sizeof(local));
        printf("rtc: set, local %s\n", local);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_sensors(int argc, char **argv)
{
    return diag_on_owner(sensors_body, argc, argv);
}

static int cmd_battery(int argc, char **argv)
{
    return diag_on_owner(battery_body, argc, argv);
}

static int cmd_rtc(int argc, char **argv)
{
    return diag_on_owner(rtc_body, argc, argv);
}

esp_err_t diag_register_sensor_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "sensors", .help = "Read the SHTC3 now", .func = &cmd_sensors },
        { .command = "battery", .help = "Read the battery now and show the gauge; battery learn start|stop",
          .func = &cmd_battery },
        { .command = "rtc", .help = "rtc get | rtc set <ISO 8601 time; Z, +HH:MM or local>", .func = &cmd_rtc },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
