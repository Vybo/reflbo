#include "lang.h"

#include <stdio.h>
#include <string.h>

extern const lang_t lang_en;
extern const lang_t lang_cs;

static const lang_t *const k_packs[] = { &lang_en, &lang_cs };

const lang_t *lang_get(const char *code)
{
    for (size_t i = 0; code != NULL && i < sizeof(k_packs) / sizeof(k_packs[0]); i++) {
        if (strcmp(k_packs[i]->code, code) == 0) {
            return k_packs[i];
        }
    }
    return &lang_en;
}

const char *lang_str(const lang_t *lang, lang_str_t id)
{
    return (unsigned)id < LS_COUNT && lang->strings[id] != NULL ? lang->strings[id] : "";
}

void lang_format_date(const lang_t *lang, const struct tm *tm, lang_date_style_t style, char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (tm->tm_wday < 0 || tm->tm_wday > 6 || tm->tm_mon < 0 || tm->tm_mon > 11 || tm->tm_mday < 1 ||
        tm->tm_mday > 31) {
        return;
    }
    lang->format_date(lang, tm, style, out, size);
}

void lang_format_time(int hour, int minute, int second, bool h24, bool seconds, char *out, size_t size,
                      const char **suffix)
{
    *suffix = "";
    if (!h24) {
        *suffix = hour < 12 ? "AM" : "PM";
        hour = hour % 12 == 0 ? 12 : hour % 12;
    }
    if (seconds) {
        snprintf(out, size, h24 ? "%02d:%02d:%02d" : "%d:%02d:%02d", hour, minute, second);
    } else {
        snprintf(out, size, h24 ? "%02d:%02d" : "%d:%02d", hour, minute);
    }
}

int lang_format_decimal(const lang_t *lang, long value, int decimals, char *out, size_t size)
{
    long scale = 1;
    for (int i = 0; i < decimals; i++) {
        scale *= 10;
    }
    const char *sign = value < 0 ? "-" : "";
    unsigned long magnitude = value < 0 ? 0ul - (unsigned long)value : (unsigned long)value;
    if (decimals <= 0) {
        return snprintf(out, size, "%s%lu", sign, magnitude);
    }
    return snprintf(out, size, "%s%lu%c%0*lu", sign, magnitude / (unsigned long)scale, lang->decimal_sep, decimals,
                    magnitude % (unsigned long)scale);
}
