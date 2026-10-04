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
    case LANG_DATE_DAY:
        snprintf(out, size, "%s %d.", lang->weekdays_short[tm->tm_wday], tm->tm_mday);
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
        [LS_M_WIFI] = "Wi-Fi",
        [LS_M_CONFIG_MODE] = "Režim nastavení",
        [LS_M_FORGET_NETWORKS] = "Zapomenout sítě",
        [LS_M_RESET_PASSWORD] = "Resetovat heslo webu",
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
        [LS_M_IP] = "IP adresa",
        [LS_M_MAC] = "MAC adresa",
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
        [LS_CONFIRM_FORGET_NETWORKS] = "Zapomenout všechny uložené sítě Wi-Fi?",
        [LS_CONFIRM_RESET_PASSWORD] = "Smazat heslo webu? Při další návštěvě zvolíte nové.",
        [LS_T_PRESET] = "Předvolba",
        [LS_T_CYCLE_ON] = "Střídání zapnuto",
        [LS_T_CYCLE_OFF] = "Střídání vypnuto",
        [LS_T_DEFAULTS] = "Výchozí nastavení",
        [LS_T_NIGHT_UNTIL] = "Noc do",
        [LS_T_REBOOTING] = "Restartuji…",
        [LS_T_RESETTING] = "Obnovuji tovární nastavení…",
        [LS_T_NETWORKS_FORGOTTEN] = "Sítě zapomenuty",
        [LS_T_PASSWORD_CLEARED] = "Heslo webu smazáno",
        [LS_T_WIFI_OFF] = "Wi-Fi vypnuto",
        [LS_T_UPDATED] = "Firmware aktualizován",
        [LS_BATTERY_EMPTY] = "Baterie je vybitá",
        [LS_CHARGE_ME] = "Nabijte mě, prosím",
        [LS_C_TITLE] = "Nastavení Wi-Fi",
        [LS_C_CLOSES_IN] = "konec za",
        [LS_C_STARTING] = "Zapínám Wi-Fi…",
        [LS_C_CONNECTING] = "Připojuji k síti",
        [LS_C_CONNECTED] = "Připojeno k síti",
        [LS_C_NETWORK] = "Síť Wi-Fi",
        [LS_C_PASSWORD] = "Heslo",
        [LS_C_THEN_OPEN] = "Pak otevřete",
        [LS_C_ADDRESS] = "Otevřete v prohlížeči",
        [LS_C_SCAN_JOIN] = "Připojit se",
        [LS_C_SCAN_OPEN] = "Otevřít",
        [LS_HINT_CONFIG] = "Podržte BOOT pro vypnutí Wi-Fi",
        [LS_HINT_CONFIG_SWITCH] = "KEY jiný kód     Podržte BOOT pro vypnutí Wi-Fi",
        [LS_HINT_CONFIG_BACK] = "KEY zpět     Podržte BOOT pro vypnutí Wi-Fi",
        [LS_F_TITLE] = "Vítejte",
        [LS_F_WIFI] = "Podržte BOOT 3 s pro nastavení Wi-Fi",
        [LS_F_MENU] = "Podržte KEY pro menu",
        [LS_F_CONTINUE] = "Stiskněte KEY pro pokračování",
        [LS_AIR_QUALITY] = "Kvalita ovzduší",
        [LS_PM25] = "PM2,5",
        [LS_PM10] = "PM10",
        [LS_POLLEN] = "Pyl",
        [LS_POLLEN_ALDER] = "Olše",
        [LS_POLLEN_BIRCH] = "Bříza",
        [LS_POLLEN_GRASS] = "Trávy",
        [LS_POLLEN_MUGWORT] = "Pelyněk",
        [LS_POLLEN_OLIVE] = "Olivovník",
        [LS_POLLEN_RAGWEED] = "Ambrozie",
        [LS_AQ_GOOD] = "Dobrá",
        [LS_AQ_FAIR] = "Přijatelná",
        [LS_AQ_MODERATE] = "Zhoršená",
        [LS_AQ_POOR] = "Špatná",
        [LS_AQ_VERY_POOR] = "Velmi špatná",
        [LS_AQ_EXTREMELY_POOR] = "Mimořádně špatná",
        [LS_POLLEN_NONE] = "Žádný",
        [LS_POLLEN_LOW] = "Nízký",
        [LS_POLLEN_MODERATE] = "Střední",
        [LS_POLLEN_HIGH] = "Vysoký",
        [LS_WX_CLEAR] = "Jasno",
        [LS_WX_MAINLY_CLEAR] = "Skoro jasno",
        [LS_WX_PARTLY_CLOUDY] = "Polojasno",
        [LS_WX_OVERCAST] = "Zataženo",
        [LS_WX_FOG] = "Mlha",
        [LS_WX_DRIZZLE] = "Mrholení",
        [LS_WX_RAIN] = "Déšť",
        [LS_WX_FREEZING_RAIN] = "Mrznoucí déšť",
        [LS_WX_SNOW] = "Sněžení",
        [LS_WX_SHOWERS] = "Přeháňky",
        [LS_WX_SNOW_SHOWERS] = "Sněhové přeháňky",
        [LS_WX_THUNDERSTORM] = "Bouřka",
        [LS_WX_UNKNOWN] = "Počasí",
        [LS_FEELS_LIKE] = "Pocitově",
        [LS_WIND] = "Vítr",
        [LS_SUNRISE] = "Východ",
        [LS_SUNSET] = "Západ",
        [LS_DAY_LENGTH] = "Délka dne",
        [LS_POLAR_DAY] = "Polární den",
        [LS_POLAR_NIGHT] = "Polární noc",
        [LS_M_SYNC] = "Synchronizace",
        [LS_M_SYNC_NOW] = "Synchronizovat",
        [LS_M_SYNC_MODE] = "Plán",
        [LS_M_SYNC_INTERVAL] = "Interval",
        [LS_M_QUIET_HOURS] = "Tiché hodiny",
        [LS_M_LAST_SYNC] = "Poslední synch.",
        [LS_SYNC_TIMES] = "V daný čas",
        [LS_SYNC_INTERVAL] = "Pravidelně",
        [LS_SYNC_ALWAYS] = "Stále",
        [LS_SYNC_MANUAL] = "Ručně",
        [LS_SYNC_STEP_WIFI] = "Wi-Fi",
        [LS_SYNC_STEP_TIME] = "Čas",
        [LS_SYNC_STEP_WEATHER] = "Počasí",
        [LS_SYNC_STEP_AIR] = "Ovzduší",
        [LS_SYNC_STEP_RADAR] = "Radar",
        [LS_SYNC_NEVER] = "Nikdy",
        [LS_SYNC_RUNNING] = "Probíhá",
        [LS_T_SYNC_STARTED] = "Synchronizuji…",
        [LS_T_SYNC_DONE] = "Synchronizováno",
        [LS_T_SYNC_FAILED] = "Synchronizace selhala",
        [LS_T_NO_NETWORK] = "Žádná uložená síť Wi-Fi",
        [LS_T_ALWAYS_ON] = "Stále: Wi-Fi zůstává zapnutá",
        [LS_T_ALWAYS_FROM] = "Stále: Wi-Fi od",
        [LS_T_SYNC_MODE] = "Synchronizace",
        [LS_UV_INDEX] = "UV index",
        [LS_UV_LOW] = "Nízký",
        [LS_UV_MODERATE] = "Střední",
        [LS_UV_HIGH] = "Vysoký",
        [LS_UV_VERY_HIGH] = "Velmi vysoký",
        [LS_UV_EXTREME] = "Extrémní",
        [LS_RAIN_2H] = "Déšť do 2 h",
        [LS_RAIN_NOW] = "Prší",
        [LS_RAIN_FROM] = "Déšť od",
        [LS_DRY_2H] = "2 h bez deště",
        [LS_MM_PER_H] = "mm/h",
        [LS_RAIN_MAP] = "Mapa srážek",
        [LS_NO_RADAR_FRAME] = "Zatím žádný snímek radaru",
        [LS_RAIN_LIGHT] = "slabý",
        [LS_RAIN_MODERATE] = "mírný",
        [LS_RAIN_HEAVY] = "silný",
        [LS_AGO] = "před %s",
        [LS_FLIGHTS_NEED_ALWAYS] = "Lety jen v synchronizaci Stále",
        [LS_NO_AIRCRAFT] = "Do %s žádná letadla",
        [LS_NO_AIRCRAFT_DATA] = "Žádná data o letadlech",
        [LS_DIR_N] = "S",
        [LS_DIR_NE] = "SV",
        [LS_DIR_E] = "V",
        [LS_DIR_SE] = "JV",
        [LS_DIR_S] = "J",
        [LS_DIR_SW] = "JZ",
        [LS_DIR_W] = "Z",
        [LS_DIR_NW] = "SZ",
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
