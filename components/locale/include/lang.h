#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/*
 * Language packs (spec §5.8): UI strings, weekday, month and moon-phase names, and the date, time
 * and number formats. The component is `locale`; the prefix is `lang_` because libc's <locale.h>
 * already owns `locale_t`. Pure C, host-buildable.
 */

typedef enum {
    LS_SET_TIME,
    LS_TIME,
    LS_DATE,
    LS_TEMPERATURE,
    LS_HUMIDITY,
    LS_DEW_POINT,
    LS_TODAY_MIN,
    LS_TODAY_MAX,
    LS_BATTERY,
    LS_BATTERY_DAYS,
    LS_WEEK,
    LS_MOON,
    LS_NAME_DAY,
    LS_HOLIDAY,
    LS_WEATHER,
    LS_TODAY,
    LS_FORECAST,
    LS_SUN,
    LS_DAYS_UNIT,    /* after a number of days: "d" */
    LS_HOURS_UNIT,   /* "h" */
    LS_MINUTES_UNIT, /* "min" */
    /* The menu (spec §5.7) */
    LS_MENU,
    LS_M_PRESETS,
    LS_M_ACTIVE_PRESET,
    LS_M_AUTO_CYCLE,
    LS_M_CYCLE_INTERVAL,
    LS_M_SCHEDULE,
    LS_M_WIFI,
    LS_M_CONFIG_MODE,
    LS_M_FORGET_NETWORKS,
    LS_M_RESET_PASSWORD,
    LS_M_TIME,
    LS_M_SET_DATETIME,
    LS_M_CLOCK_24H,
    LS_M_TIME_ZONE,
    LS_M_DISPLAY,
    LS_M_UPDATE_INTERVAL,
    LS_M_REFRESH_RATE,
    LS_M_SENSORS,
    LS_M_TEMP_OFFSET,
    LS_M_HUM_OFFSET,
    LS_M_UNITS,
    LS_M_INFO,
    LS_M_FIRMWARE,
    LS_M_DEVICE,
    LS_M_IP,
    LS_M_MAC,
    LS_M_UPTIME,
    LS_M_FREE_MEMORY,
    LS_M_SYSTEM,
    LS_M_LANGUAGE,
    LS_M_REBOOT,
    LS_M_FACTORY_RESET,
    LS_ON,
    LS_OFF,
    LS_HINT_BROWSE,   /* the button hints at the bottom of the menu (spec §5.6) */
    LS_HINT_EDIT,
    LS_HINT_DATETIME,
    LS_HINT_CONFIRM,
    LS_CONFIRM_FACTORY_RESET,
    LS_CONFIRM_FORGET_NETWORKS,
    LS_CONFIRM_RESET_PASSWORD,
    /* Toasts and special screens (spec §5.5) */
    LS_T_PRESET, /* followed by ": <preset name>" */
    LS_T_CYCLE_ON,
    LS_T_CYCLE_OFF,
    LS_T_DEFAULTS, /* a config file was invalid, so the defaults are in use */
    LS_T_NIGHT_UNTIL, /* followed by " 06:00" */
    LS_T_REBOOTING,
    LS_T_RESETTING,
    LS_T_NETWORKS_FORGOTTEN,
    LS_T_PASSWORD_CLEARED,
    LS_T_WIFI_OFF,  /* config mode ended */
    LS_T_UPDATED,   /* a firmware upload finished; the board restarts */
    LS_BATTERY_EMPTY,
    LS_CHARGE_ME,
    /* Config mode and the first run (spec §5.5, §10.2) */
    LS_C_TITLE,
    LS_C_CLOSES_IN, /* followed by " 9 min" */
    LS_C_STARTING,
    LS_C_CONNECTING, /* a label; the network's name follows below it */
    LS_C_CONNECTED,
    LS_C_NETWORK,
    LS_C_PASSWORD,
    LS_C_THEN_OPEN,
    LS_C_ADDRESS,
    LS_C_SCAN_JOIN, /* what the QR code does */
    LS_C_SCAN_OPEN,
    LS_HINT_CONFIG,
    LS_HINT_CONFIG_SWITCH, /* with a second QR code to switch to */
    LS_F_TITLE,
    LS_F_WIFI,
    LS_F_MENU,
    LS_F_CONTINUE,
    LS_COUNT,
} lang_str_t;

typedef enum {
    LANG_DATE_LONG,   /* Friday 25 September */
    LANG_DATE_MEDIUM, /* Fri 25 Sep */
    LANG_DATE_SHORT,  /* 25 Sep */
} lang_date_style_t;

typedef struct lang {
    const char *code; /* "en" */
    const char *name; /* in the language itself: "English" */
    const char *strings[LS_COUNT];
    const char *weekdays[7]; /* struct tm order: Sunday first */
    const char *weekdays_short[7];
    const char *months[12];
    const char *months_short[12];
    const char *moon_phases[8];       /* util_moon_t.index order */
    const char *moon_phases_short[8]; /* for small slots; the disc shows waxing or waning */
    char decimal_sep;
    int first_weekday; /* 0 Sunday, 1 Monday */
    void (*format_date)(const struct lang *lang, const struct tm *tm, lang_date_style_t style, char *out,
                        size_t size);
    const char *(*name_day)(int month, int day);         /* NULL: the pack has no calendar */
    const char *(*holiday)(int year, int month, int day); /* NULL, or NULL result: not a holiday */
} lang_t;

/* The pack for a code such as "en"; English for NULL or an unknown code. */
const lang_t *lang_get(const char *code);
const char *lang_str(const lang_t *lang, lang_str_t id);
/* An out-of-range tm writes an empty string. */
void lang_format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size);
/* "20:48", "20:48:05", or "8:48" with *suffix "PM" in 12-hour mode (*suffix is "" otherwise). */
void lang_format_time(int hour, int minute, int second, bool h24, bool seconds, char *out, size_t size,
                      const char **suffix);
/* `value` scaled by 10^decimals: (-5, 1) is "-0.5" and (234, 1) is "23.4", with the pack's
 * decimal separator. Returns the length written. */
int lang_format_decimal(const lang_t *lang, long value, int decimals, char *out, size_t size);
