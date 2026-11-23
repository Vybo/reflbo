#include <stdio.h>

#include "lang.h"
#include "util_calendar.h"
#include "util_time.h"

/* Polish. Dates use the genitive month: "Poniedziałek 25 września". */

static void format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size)
{
    switch (style) {
    case LANG_DATE_LONG:
        snprintf(out, size, "%s %d %s", lang->weekdays[tm->tm_wday], tm->tm_mday, lang->months[tm->tm_mon]);
        break;
    case LANG_DATE_MEDIUM:
        snprintf(out, size, "%s %d %s", lang->weekdays_short[tm->tm_wday], tm->tm_mday, lang->months_short[tm->tm_mon]);
        break;
    case LANG_DATE_SHORT:
        snprintf(out, size, "%d %s", tm->tm_mday, lang->months_short[tm->tm_mon]);
        break;
    case LANG_DATE_DAY:
        snprintf(out, size, "%s %d", lang->weekdays_short[tm->tm_wday], tm->tm_mday);
        break;
    }
}

/* Public holidays in Poland (Ustawa z dnia 18 stycznia 1951 r. o dniach wolnych od pracy). */
static const char *holiday(int year, int month, int day)
{
    static const struct {
        unsigned char month, day;
        const char *name;
    } k_fixed[] = {
        { 1, 1, "Nowy Rok" },
        { 1, 6, "Trzech Króli" },
        { 5, 1, "Święto Pracy" },
        { 5, 3, "Święto Konstytucji 3 Maja" },
        { 8, 15, "Wniebowzięcie NMP" },
        { 11, 1, "Wszystkich Świętych" },
        { 11, 11, "Święto Niepodległości" },
        { 12, 25, "Boże Narodzenie" },
        { 12, 26, "Drugi Dzień Świąt" },
    };
    for (size_t i = 0; i < sizeof(k_fixed) / sizeof(k_fixed[0]); i++) {
        if (k_fixed[i].month == month && k_fixed[i].day == day) {
            return k_fixed[i].name;
        }
    }
    
    int em, ed;
    util_easter(year, &em, &ed);
    int64_t easter = util_days_from_civil(year, em, ed);
    int64_t today = util_days_from_civil(year, month, day);
    
    if (today == easter) {
        return "Wielkanoc";
    }
    if (today == easter + 1) {
        return "Poniedziałek Wielkanocny";
    }
    if (today == easter + 60) {
        return "Boże Ciało";
    }
    return NULL;
}

const lang_t lang_pl = {
    .code = "pl",
    .name = "Polski",
    .strings = {
        [LS_SET_TIME] = "Ustaw czas",
        [LS_TIME] = "Czas",
        [LS_DATE] = "Data",
        [LS_TEMPERATURE] = "Temperatura",
        [LS_HUMIDITY] = "Wilgotność",
        [LS_DEW_POINT] = "Punkt rosy",
        [LS_TODAY_MIN] = "Dzisiejsze minimum",
        [LS_TODAY_MAX] = "Dzisiejsze maksimum",
        [LS_BATTERY] = "Bateria",
        [LS_BATTERY_DAYS] = "Czas pracy baterii",
        [LS_WEEK] = "Tydzień",
        [LS_MOON] = "Księżyc",
        [LS_NAME_DAY] = "Imieniny",
        [LS_HOLIDAY] = "Święto",
        [LS_WEATHER] = "Pogoda",
        [LS_TODAY] = "Dziś",
        [LS_FORECAST] = "Prognoza",
        [LS_SUN] = "Wschód i zachód słońca",
        [LS_DAYS_UNIT] = "d",
        [LS_HOURS_UNIT] = "h",
        [LS_MINUTES_UNIT] = "min",
        [LS_MENU] = "Menu",
        [LS_M_PRESETS] = "Profile",
        [LS_M_ACTIVE_PRESET] = "Aktywny profil",
        [LS_M_AUTO_CYCLE] = "Automatyczna zmiana",
        [LS_M_CYCLE_INTERVAL] = "Interwał zmiany",
        [LS_M_SCHEDULE] = "Harmonogram",
        [LS_M_WIFI] = "Wi-Fi",
        [LS_M_CONFIG_MODE] = "Tryb konfiguracji",
        [LS_M_FORGET_NETWORKS] = "Zapomnij sieci",
        [LS_M_RESET_PASSWORD] = "Resetuj hasło panelu",
        [LS_M_TIME] = "Czas",
        [LS_M_SET_DATETIME] = "Ustaw datę i czas",
        [LS_M_CLOCK_24H] = "Zegar 24-godzinny",
        [LS_M_TIME_ZONE] = "Strefa czasowa",
        [LS_M_DISPLAY] = "Ekran",
        [LS_M_UPDATE_INTERVAL] = "Interwał odświeżania",
        [LS_M_REFRESH_RATE] = "Częstotliwość odświeżania",
        [LS_M_SENSORS] = "Czujniki",
        [LS_M_TEMP_OFFSET] = "Korekta temperatury",
        [LS_M_HUM_OFFSET] = "Korekta wilgotności",
        [LS_M_UNITS] = "Jednostki",
        [LS_M_INFO] = "Informacje",
        [LS_M_FIRMWARE] = "Firmware",
        [LS_M_DEVICE] = "Urządzenie",
        [LS_M_IP] = "Adres IP",
        [LS_M_MAC] = "Adres MAC",
        [LS_M_UPTIME] = "Czas pracy",
        [LS_M_FREE_MEMORY] = "Wolna pamięć",
        [LS_M_SYSTEM] = "System",
        [LS_M_LANGUAGE] = "Język",
        [LS_M_REBOOT] = "Uruchom ponownie",
        [LS_M_FACTORY_RESET] = "Ustawienia fabryczne",
        [LS_ON] = "Wł.",
        [LS_OFF] = "Wył.",
        [LS_HINT_BROWSE] = "KEY następny · przytrzymaj: otwórz     BOOT wstecz · przytrzymaj: zamknij",
        [LS_HINT_EDIT] = "KEY + · przytrzymaj: zapisz     BOOT – · przytrzymaj: anuluj",
        [LS_HINT_DATETIME] = "KEY + · przytrzymaj: następny     BOOT – · przytrzymaj: anuluj",
        [LS_HINT_CONFIRM] = "Przytrzymaj KEY aby potwierdzić · BOOT anuluje",
        [LS_CONFIRM_FACTORY_RESET] = "Usunąć wszystkie ustawienia i profile?",
        [LS_CONFIRM_FORGET_NETWORKS] = "Zapomnieć wszystkie zapisane sieci Wi-Fi?",
        [LS_CONFIRM_RESET_PASSWORD] = "Usunąć hasło panelu? Przy kolejnej wizycie ustawisz nowe.",
        [LS_T_PRESET] = "Profil",
        [LS_T_CYCLE_ON] = "Zmiana włączona",
        [LS_T_CYCLE_OFF] = "Zmiana wyłączona",
        [LS_T_DEFAULTS] = "Ustawienia domyślne",
        [LS_T_NIGHT_UNTIL] = "Noc do",
        [LS_T_REBOOTING] = "Restartowanie…",
        [LS_T_RESETTING] = "Przywracanie ustawień fabrycznych…",
        [LS_T_NETWORKS_FORGOTTEN] = "Sieci zapomniane",
        [LS_T_PASSWORD_CLEARED] = "Hasło panelu usunięte",
        [LS_T_WIFI_OFF] = "Wi-Fi wyłączone",
        [LS_T_UPDATED] = "Firmware zaktualizowany",
        [LS_BATTERY_EMPTY] = "Bateria rozładowana",
        [LS_CHARGE_ME] = "Proszę mnie naładować",
        [LS_C_TITLE] = "Konfiguracja Wi-Fi",
        [LS_C_CLOSES_IN] = "koniec za",
        [LS_C_STARTING] = "Uruchamianie Wi-Fi…",
        [LS_C_CONNECTING] = "Łączenie z siecią",
        [LS_C_CONNECTED] = "Połączono z siecią",
        [LS_C_NETWORK] = "Sieć Wi-Fi",
        [LS_C_PASSWORD] = "Hasło",
        [LS_C_THEN_OPEN] = "Następnie otwórz",
        [LS_C_ADDRESS] = "Otwórz w przeglądarce",
        [LS_C_SCAN_JOIN] = "Połącz się",
        [LS_C_SCAN_OPEN] = "Otwórz",
        [LS_HINT_CONFIG] = "Przytrzymaj BOOT, aby wyłączyć Wi-Fi",
        [LS_HINT_CONFIG_SWITCH] = "KEY inny kod     Przytrzymaj BOOT, aby wyłączyć Wi-Fi",
        [LS_HINT_CONFIG_BACK] = "KEY wstecz     Przytrzymaj BOOT, aby wyłączyć Wi-Fi",
        [LS_F_TITLE] = "Witamy",
        [LS_F_WIFI] = "Przytrzymaj BOOT 3 s, aby skonfigurować Wi-Fi",
        [LS_F_MENU] = "Przytrzymaj KEY, aby wejść do menu",
        [LS_F_CONTINUE] = "Naciśnij KEY, aby kontynuować",
        [LS_AIR_QUALITY] = "Jakość powietrza",
        [LS_PM25] = "PM2.5",
        [LS_PM10] = "PM10",
        [LS_POLLEN] = "Pyłki",
        [LS_POLLEN_ALDER] = "Olsza",
        [LS_POLLEN_BIRCH] = "Brzoza",
        [LS_POLLEN_GRASS] = "Trawy",
        [LS_POLLEN_MUGWORT] = "Bylica",
        [LS_POLLEN_OLIVE] = "Oliwka",
        [LS_POLLEN_RAGWEED] = "Ambrozja",
        [LS_AQ_GOOD] = "Dobra",
        [LS_AQ_FAIR] = "Aceptowalna",
        [LS_AQ_MODERATE] = "Umiarkowana",
        [LS_AQ_POOR] = "Zła",
        [LS_AQ_VERY_POOR] = "Bardzo zła",
        [LS_AQ_EXTREMELY_POOR] = "Fatalna",
        [LS_POLLEN_NONE] = "Brak",
        [LS_POLLEN_LOW] = "Niski",
        [LS_POLLEN_MODERATE] = "Średni",
        [LS_POLLEN_HIGH] = "Wysoki",
        [LS_WX_CLEAR] = "Jasno",
        [LS_WX_MAINLY_CLEAR] = "Prawie jasno",
        [LS_WX_PARTLY_CLOUDY] = "Zachmurzenie umiarkowane",
        [LS_WX_OVERCAST] = "Pochmurno",
        [LS_WX_FOG] = "Mgła",
        [LS_WX_DRIZZLE] = "Mżawka",
        [LS_WX_RAIN] = "Deszcz",
        [LS_WX_FREEZING_RAIN] = "Marznący deszcz",
        [LS_WX_SNOW] = "Śnieg",
        [LS_WX_SHOWERS] = "Przelotne opady",
        [LS_WX_SNOW_SHOWERS] = "Przelotny śnieg",
        [LS_WX_THUNDERSTORM] = "Burza",
        [LS_WX_UNKNOWN] = "Pogoda",
        [LS_FEELS_LIKE] = "Odczuwalna",
        [LS_WIND] = "Wiatr",
        [LS_SUNRISE] = "Wschód",
        [LS_SUNSET] = "Zachód",
        [LS_DAY_LENGTH] = "Długość dnia",
        [LS_POLAR_DAY] = "Dzień polarny",
        [LS_POLAR_NIGHT] = "Noc polarna",
        [LS_M_SYNC] = "Synchronizacja",
        [LS_M_SYNC_NOW] = "Synchronizuj teraz",
        [LS_M_SYNC_MODE] = "Harmonogram",
        [LS_M_SYNC_INTERVAL] = "Interwał",
        [LS_M_QUIET_HOURS] = "Godziny ciszy",
        [LS_M_LAST_SYNC] = "Ostatnia synch.",
        [LS_SYNC_TIMES] = "O zadanej godzinie",
        [LS_SYNC_INTERVAL] = "Regularnie",
        [LS_SYNC_ALWAYS] = "Zawsze włączone",
        [LS_SYNC_MANUAL] = "Ręcznie",
        [LS_SYNC_STEP_WIFI] = "Wi-Fi",
        [LS_SYNC_STEP_TIME] = "Czas",
        [LS_SYNC_STEP_WEATHER] = "Pogoda",
        [LS_SYNC_STEP_AIR] = "Powietrze",
        [LS_SYNC_STEP_RADAR] = "Radar",
        [LS_SYNC_STEP_SOLAR] = "Prognoza PV",
        [LS_SYNC_STEP_ENERGY] = "Energia domu",
        [LS_SYNC_STEP_PC] = "Komputer",
        [LS_SYNC_NEVER] = "Nigdy",
        [LS_SYNC_RUNNING] = "W toku",
        [LS_T_SYNC_STARTED] = "Synchronizowanie…",
        [LS_T_SYNC_DONE] = "Zsynchronizowano",
        [LS_T_SYNC_FAILED] = "Synch. nie powiodła się",
        [LS_T_NO_NETWORK] = "Brak zapisanej sieci Wi-Fi",
        [LS_T_ALWAYS_ON] = "Zawsze: Wi-Fi pozostaje włączone",
        [LS_T_ALWAYS_FROM] = "Zawsze: Wi-Fi od",
        [LS_T_SYNC_MODE] = "Synchronizacja",
        [LS_UV_INDEX] = "Indeks UV",
        [LS_UV_LOW] = "Niski",
        [LS_UV_MODERATE] = "Średni",
        [LS_UV_HIGH] = "Wysoki",
        [LS_UV_VERY_HIGH] = "Bardzo wysoki",
        [LS_UV_EXTREME] = "Ekstremalny",
        [LS_RAIN_2H] = "Deszcz w ciągu 2 h",
        [LS_RAIN_NOW] = "Pada deszcz",
        [LS_RAIN_FROM] = "Deszcz od",
        [LS_DRY_2H] = "2 h bez deszczu",
        [LS_MM_PER_H] = "mm/h",
        [LS_RAIN_MAP] = "Mapa opadów",
        [LS_NO_RADAR_FRAME] = "Brak danych radaru",
        [LS_RAIN_LIGHT] = "słaby",
        [LS_RAIN_MODERATE] = "umiarkowany",
        [LS_RAIN_HEAVY] = "silny",
        [LS_AGO] = "%s temu",
        [LS_FLIGHTS_NEED_ALWAYS] = "Loty wymagają trybu Zawsze włączone",
        [LS_NO_AIRCRAFT] = "Brak samolotów w promieniu %s",
        [LS_NO_AIRCRAFT_DATA] = "Brak danych o lotach",
        [LS_DIR_N] = "N",
        [LS_DIR_NE] = "NE",
        [LS_DIR_E] = "E",
        [LS_DIR_SE] = "SE",
        [LS_DIR_S] = "S",
        [LS_DIR_SW] = "SW",
        [LS_DIR_W] = "W",
        [LS_DIR_NW] = "NW",
        [LS_PV_NOW] = "Prognoza teraz",
        [LS_PV_TODAY] = "Prognoza na dziś",
        [LS_PV_LEFT] = "Jeszcze dzisiaj",
        [LS_PV_TOMORROW] = "Prognoza na jutro",
        [LS_PV_PEAK] = "Szczyt",
        [LS_EN_PV] = "Fotowoltaika",
        [LS_EN_GRID] = "Sieć",
        [LS_EN_LOAD] = "Dom",
        [LS_EN_BATTERY] = "Magazyn energii",
        [LS_EN_YIELD] = "Wyprodukowano dziś",
        [LS_EN_EXPORT] = "Do sieci dziś",
        [LS_EN_IMPORT] = "Z sieci dziś",
        [LS_EN_SELF] = "Autokonsumpcja",
        [LS_EN_EXPORTING] = "Oddawanie",
        [LS_EN_IMPORTING] = "Pobór",
        [LS_PV_CHART] = "Prognoza PV",
        [LS_EN_FLOW] = "Przepływ energii",
        [LS_NOW] = "Teraz",
        [LS_EN_CHARGING] = "Ładowanie",
        [LS_EN_DISCHARGING] = "Rozładowywanie",
        [LS_EN_PRODUCED] = "Wyprodukowano",
        [LS_EN_EXPORTED] = "Do sieci",
        [LS_EN_IMPORTED] = "Z sieci",
        [LS_NO_SOLAR] = "Brak prognozy PV",
        [LS_NO_ENERGY] = "Brak danych z falownika",
        [LS_M_SYNC_STEPS] = "Kroki",
        [LS_PC] = "Komputer",
        [LS_PC_ASLEEP] = "Komputer uśpiony",
        [LS_PC_SINCE] = "od %s",
        [LS_M_PC_STATS] = "Statystyki PC",
        [LS_M_PC_SESSION] = "Sesja",
        [LS_PC_NONE] = "Brak",
        [LS_PC_OFF] = "Wyłączony",
    },
    .weekdays = { "Niedziela", "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota" },
    .weekdays_short = { "Nie", "Pon", "Wto", "Śro", "Czw", "Pią", "Sob" },
    /* Formy dopełniacza (genitive) używane w datach długich */
    .months = { "stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca", "lipca", "sierpnia", "września", "października",
    "listopada", "grudnia" },
    .months_short = { "sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru" },
    .moon_phases = { "Nów", "Przybierający sierp", "Pierwsza kwadra", "Przybierający księżyc", "Pełnia", "Ubywający księżyc",
    "Ostatnia kwadra", "Ubywający sierp" },
    .moon_phases_short = { "Nów", "Sierp", "1. kwadra", "Przybywa", "Pełnia", "Ubywa", "Ostat. kw.", "Sierp" },
    .decimal_sep = ',',
    .first_weekday = 1,
    .format_date = format_date,
    .name_day = NULL,
    .holiday = holiday,
};