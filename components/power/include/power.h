#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "power_policy.h"

/*
 * Power states and sleep entry (spec §3.4, §9). Light sleep is entered by power_sleep_light();
 * deep sleep by power_sleep_deep(), which reboots into app_main on wake. Either one drops the
 * USB console, so the policy keeps a tethered board awake. Call from the app task only.
 */

typedef enum {
    POWER_WAKE_COLD,  /* power-on or reset, not a wake from sleep */
    POWER_WAKE_RTC,   /* PCF85063 alarm (INT low) */
    POWER_WAKE_KEY,
    POWER_WAKE_BOOT,
    POWER_WAKE_TIMER, /* backup timer: the RTC alarm did not arrive */
    POWER_WAKE_OTHER,
    POWER_WAKE_COUNT,
} power_wake_t;

typedef struct {
    uint32_t light_sleeps;
    uint32_t deep_sleeps;
    uint32_t wakes[POWER_WAKE_COUNT];
    /* Awake phases, from a wake to the next sleep; ROM and bootloader time are not included. The
     * phase in which the stats were reset is not counted. */
    uint32_t awake_count;
    uint64_t awake_ms_total;
    uint32_t awake_ms_last;
    uint32_t awake_ms_max;
    /* Sleeps, from entry to wake; a deep sleep also includes the ROM and bootloader start-up. */
    uint32_t slept_count;
    uint64_t slept_ms_total;
    uint32_t slept_ms_last;
    uint32_t slept_ms_min;
} power_stats_t;

/* Call once at boot, before anything else: decodes and counts the wake cause. The idle strategy
 * comes from the copy in RTC RAM until power_load_settings() runs. */
esp_err_t power_init(void);
/* Reads the settings from NVS (after nvs_flash_init()). Routine deep-sleep wakes skip NVS and use
 * the copy kept in RTC RAM. */
esp_err_t power_load_settings(void);
/* The app could not start: the policy keeps the board awake for the console while a PC is
 * attached, and returns POWER_PLAN_RETRY otherwise. On an image not yet marked valid it returns
 * POWER_PLAN_ROLLBACK instead (spec §10.5). */
void power_boot_failed(bool image_pending);
power_wake_t power_boot_wake(void); /* why this boot happened */
const char *power_wake_name(power_wake_t wake);
bool power_tethered(void);
/* Stay awake at least this long from now: lets a PC find the board after boot or a button. */
void power_hold_awake_ms(uint32_t ms);
power_plan_t power_plan(bool work_pending);
/* Sleeps until `until_utc` or a button or the RTC alarm; returns what woke it. */
power_wake_t power_sleep_light(time_t until_utc);
/* Never returns: the chip reboots on wake. Seal RTC-RAM state and hold the panel pins first. */
void power_sleep_deep(time_t until_utc);
/* Never returns: the critical-battery sleep (spec §8), woken by KEY only. If KEY is held, its
 * release wakes the chip instead, and a timer after `recheck_s` in case it is stuck. */
void power_sleep_critical(uint32_t recheck_s);
/* Never returns: deep sleep for `seconds` or until KEY or BOOT, then boot from scratch. */
void power_sleep_retry(uint32_t seconds);
/* A button held when the chip goes to sleep is left out of that sleep's wake sources (D16), so a
 * stuck KEY or BOOT can't keep the board awake. These bits say which were, for the sleep that just
 * ended: the app ignores such a button until it is released. */
#define POWER_BUTTON_KEY  (1u << 0)
#define POWER_BUTTON_BOOT (1u << 1)
unsigned power_masked_buttons(void);
power_idle_t power_idle_strategy(void);
/* Applies at once; an error means only that NVS didn't keep it for the next boot. */
esp_err_t power_set_idle_strategy(power_idle_t idle);
/* `sleep test`: the next `cycles` sleeps use `mode` even when tethered. Resets the stats. */
void power_start_test(power_idle_t mode, int cycles);
int power_test_cycles_left(void);
power_stats_t power_stats(void);
void power_reset_stats(void);
