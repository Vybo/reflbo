#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_internal.h"
#include "esp_console.h"
#include "power.h"

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

static int power_body(int argc, char **argv)
{
    static const char *const k_usage = "power idle [deep|light]";
    if (argc < 2 || strcmp(argv[1], "idle") != 0 || argc > 3) {
        return usage(k_usage);
    }
    if (argc == 3) {
        power_idle_t idle;
        if (strcmp(argv[2], "deep") == 0) {
            idle = POWER_IDLE_DEEP;
        } else if (strcmp(argv[2], "light") == 0) {
            idle = POWER_IDLE_LIGHT;
        } else {
            return usage(k_usage);
        }
        esp_err_t err = power_set_idle_strategy(idle);
        if (err != ESP_OK) {
            printf("power: set for this session, not saved: %s\n", esp_err_to_name(err));
        }
    }
    printf("power: idle %s%s\n", power_idle_strategy() == POWER_IDLE_DEEP ? "deep" : "light",
           power_tethered() ? " (tethered: stays awake while a USB host is connected)" : "");
    return 0;
}

static int sleep_body(int argc, char **argv)
{
    static const char *const k_usage = "sleep stats [reset] | sleep test <deep|light> <cycles>";
    if (argc >= 2 && strcmp(argv[1], "stats") == 0) {
        if (argc == 3 && strcmp(argv[2], "reset") == 0) {
            power_reset_stats();
        } else if (argc != 2) {
            return usage(k_usage);
        }
        power_stats_t st = power_stats();
        printf("sleep: %lu light, %lu deep; wakes: rtc %lu, key %lu, boot %lu, timer %lu, other %lu\n",
               (unsigned long)st.light_sleeps, (unsigned long)st.deep_sleeps,
               (unsigned long)st.wakes[POWER_WAKE_RTC], (unsigned long)st.wakes[POWER_WAKE_KEY],
               (unsigned long)st.wakes[POWER_WAKE_BOOT], (unsigned long)st.wakes[POWER_WAKE_TIMER],
               (unsigned long)st.wakes[POWER_WAKE_OTHER]);
        printf("sleep: awake %lu times: last %lu ms, max %lu ms, mean %lu ms\n", (unsigned long)st.awake_count,
               (unsigned long)st.awake_ms_last, (unsigned long)st.awake_ms_max,
               (unsigned long)(st.awake_count ? st.awake_ms_total / st.awake_count : 0));
        printf("sleep: slept %lu times: last %lu ms, min %lu ms, mean %lu ms; test cycles left %d\n",
               (unsigned long)st.slept_count, (unsigned long)st.slept_ms_last, (unsigned long)st.slept_ms_min,
               (unsigned long)(st.slept_count ? st.slept_ms_total / st.slept_count : 0), power_test_cycles_left());
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "test") == 0) {
        power_idle_t mode;
        if (strcmp(argv[2], "deep") == 0) {
            mode = POWER_IDLE_DEEP;
        } else if (strcmp(argv[2], "light") == 0) {
            mode = POWER_IDLE_LIGHT;
        } else {
            return usage(k_usage);
        }
        int cycles = atoi(argv[3]);
        if (cycles < 1 || cycles > 1440) { /* up to a day of one-minute cycles */
            return usage(k_usage);
        }
        power_start_test(mode, cycles);
        printf("sleep: %d %s sleep cycles follow; the USB console drops and returns after them\n", cycles, argv[2]);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_power(int argc, char **argv)
{
    return diag_on_owner(power_body, argc, argv);
}

static int cmd_sleep(int argc, char **argv)
{
    return diag_on_owner(sleep_body, argc, argv);
}

esp_err_t diag_register_power_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "power", .help = "power idle [deep|light]: show or set the idle strategy", .func = &cmd_power },
        { .command = "sleep", .help = "sleep stats [reset] | sleep test <deep|light> <cycles>", .func = &cmd_sleep },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
