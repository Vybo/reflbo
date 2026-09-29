#include <stdio.h>

#include "lang.h"
#include "util_calendar.h"
#include "util_time.h"

/* Czech (spec §5.8). Dates use the genitive month: "Pátek 25. září". No name-day calendar yet:
 * it waits for a source whose licence allows redistribution (D16). */

static void format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size)
{
    switch (style) {
    case LANG_DATE_LONG:
        snprintf(out, size, "%s %d. %s", lang->weekdays[tm->tm_wday], tm->tm_mday, lang->months[tm->tm_mon]);
        break;
    case LANG_DATE_MEDIUM:
        snprintf(out, size, "%s %d. %d.", lang->weekdays_short[tm->tm_wday], tm->tm_mday, tm->tm_mon + 1);
        break;
    case LANG_DATE_SHORT:
        snprintf(out, size, "%d. %d.", tm->tm_mday, tm->tm_mon + 1);
        break;
    }
}

/* Public holidays and days off, zákon č. 245/2000 Sb. as amended (Good Friday from 2016). */
static const char *holiday(int year, int month, int day)
{
    static const struct {
        unsigned char month, day;
        const char *name;
    } k_fixed[] = {
        { 1, 1, "Nový rok" },
        { 5, 1, "Svátek práce" },
        { 5, 8, "Den vítězství" },
        { 7, 5, "Cyril a Metoděj" },
        { 7, 6, "Mistr Jan Hus" },
        { 9, 28, "Den české státnosti" },
        { 10, 28, "Vznik Československa" },
        { 11, 17, "Den boje za svobodu" },
        { 12, 24, "Štědrý den" },
        { 12, 25, "1. svátek vánoční" },
        { 12, 26, "2. svátek vánoční" },
    };
    for (size_t i = 0; i < sizeof(k_fixed) / sizeof(k_fixed[0]); i++) {
        if (k_fixed[i].month == month && k_fixed[i].day == day) {
            return k_fixed[i].name;
        }
    }
    int em, ed;
    util_easter(year, &em, &ed);
    int64_t easter = util_days_from_civil(year, em, ed), today = util_days_from_civil(year, month, day);
    if (today == easter + 1) {
        return "Velikonoční pondělí";
    }
    if (today == easter - 2 && year >= 2016) {
        return "Velký pátek";
    }
    return NULL;
}

const lang_t lang_cs = {
    .code = "cs",
    .name = "Čeština",
    .strings = {
        [LS_SET_TIME] = "Nastavte čas",
        [LS_TIME] = "Čas",
        [LS_DATE] = "Datum",
        [LS_TEMPERATURE] = "Teplota",
        [LS_HUMIDITY] = "Vlhkost",
        [LS_DEW_POINT] = "Rosný bod",
        [LS_TODAY_MIN] = "Dnešní minimum",
        [LS_TODAY_MAX] = "Dnešní maximum",
        [LS_BATTERY] = "Baterie",
        [LS_BATTERY_DAYS] = "Výdrž baterie",
        [LS_WEEK] = "Týden",
        [LS_MOON] = "Měsíc",
        [LS_NAME_DAY] = "Jmeniny",
        [LS_HOLIDAY] = "Svátek",
        [LS_WEATHER] = "Počasí",
        [LS_TODAY] = "Dnes",
        [LS_FORECAST] = "Předpověď",
        [LS_SUN] = "Východ a západ slunce",
        [LS_DAYS_UNIT] = "d",
        [LS_HOURS_UNIT] = "h",
        [LS_MINUTES_UNIT] = "min",
        [LS_MENU] = "Nabídka",
        [LS_M_PRESETS] = "Předvolby",
        [LS_M_ACTIVE_PRESET] = "Aktivní předvolba",
        [LS_M_AUTO_CYCLE] = "Střídání",
        [LS_M_CYCLE_INTERVAL] = "Interval střídání",
        [LS_M_SCHEDULE] = "Rozvrh",
        [LS_M_TIME] = "Čas",
        [LS_M_SET_DATETIME] = "Nastavit datum a čas",
        [LS_M_CLOCK_24H] = "24hodinový čas",
        [LS_M_TIME_ZONE] = "Časové pásmo",
        [LS_M_DISPLAY] = "Displej",
        [LS_M_UPDATE_INTERVAL] = "Interval obnovy",
        [LS_M_REFRESH_RATE] = "Obnovovací frekvence",
        [LS_M_SENSORS] = "Senzory",
        [LS_M_TEMP_OFFSET] = "Korekce teploty",
        [LS_M_HUM_OFFSET] = "Korekce vlhkosti",
        [LS_M_UNITS] = "Jednotky",
        [LS_M_INFO] = "Informace",
        [LS_M_FIRMWARE] = "Firmware",
        [LS_M_DEVICE] = "Zařízení",
        [LS_M_UPTIME] = "Doba běhu",
        [LS_M_FREE_MEMORY] = "Volná paměť",
        [LS_M_SYSTEM] = "Systém",
        [LS_M_LANGUAGE] = "Jazyk",
        [LS_M_REBOOT] = "Restartovat",
        [LS_M_FACTORY_RESET] = "Tovární nastavení",
        [LS_ON] = "Zapnuto",
        [LS_OFF] = "Vypnuto",
        [LS_HINT_BROWSE] = "KEY další · podržet: otevřít     BOOT zpět · podržet: zavřít",
        [LS_HINT_EDIT] = "KEY + · podržet: uložit     BOOT – · podržet: zrušit",
        [LS_HINT_DATETIME] = "KEY + · podržet: další     BOOT – · podržet: zrušit",
        [LS_HINT_CONFIRM] = "Podržte KEY pro potvrzení · BOOT zruší",
        [LS_CONFIRM_FACTORY_RESET] = "Smazat všechna nastavení a předvolby?",
        [LS_T_PRESET] = "Předvolba",
        [LS_T_CYCLE_ON] = "Střídání zapnuto",
        [LS_T_CYCLE_OFF] = "Střídání vypnuto",
        [LS_T_DEFAULTS] = "Výchozí nastavení",
        [LS_T_NIGHT_UNTIL] = "Noc do",
        [LS_T_REBOOTING] = "Restartuji…",
        [LS_T_RESETTING] = "Obnovuji tovární nastavení…",
        [LS_BATTERY_EMPTY] = "Baterie je vybitá",
        [LS_CHARGE_ME] = "Nabijte mě, prosím",
    },
    .weekdays = { "Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota" },
    .weekdays_short = { "Ne", "Po", "Út", "St", "Čt", "Pá", "So" },
    /* genitive, the form dates use */
    .months = { "ledna", "února", "března", "dubna", "května", "června", "července", "srpna", "září", "října",
                "listopadu", "prosince" },
    .months_short = { "led", "úno", "bře", "dub", "kvě", "čvn", "čvc", "srp", "zář", "říj", "lis", "pro" },
    .moon_phases = { "Nov", "Dorůstající srpek", "První čtvrť", "Dorůstající měsíc", "Úplněk", "Couvající měsíc",
                     "Poslední čtvrť", "Couvající srpek" },
    .moon_phases_short = { "Nov", "Srpek", "1. čtvrť", "Dorůstá", "Úplněk", "Couvá", "Posl. čtvrť", "Srpek" },
    .decimal_sep = ',',
    .first_weekday = 1,
    .format_date = format_date,
    .name_day = NULL,
    .holiday = holiday,
};
