#include "weather.h"

/* What the numbers mean (spec §5.1, §11). */

weather_sky_t weather_sky(uint8_t code)
{
    switch (code) {
    case 0:
        return WEATHER_SKY_CLEAR;
    case 1:
        return WEATHER_SKY_MAINLY_CLEAR;
    case 2:
        return WEATHER_SKY_PARTLY_CLOUDY;
    case 3:
        return WEATHER_SKY_OVERCAST;
    case 45:
    case 48:
        return WEATHER_SKY_FOG;
    case 51:
    case 53:
    case 55:
        return WEATHER_SKY_DRIZZLE;
    case 56:
    case 57:
    case 66:
    case 67:
        return WEATHER_SKY_FREEZING_RAIN;
    case 61:
    case 63:
    case 65:
        return WEATHER_SKY_RAIN;
    case 71:
    case 73:
    case 75:
    case 77:
        return WEATHER_SKY_SNOW;
    case 80:
    case 81:
    case 82:
        return WEATHER_SKY_SHOWERS;
    case 85:
    case 86:
        return WEATHER_SKY_SNOW_SHOWERS;
    case 95:
    case 96:
    case 99:
        return WEATHER_SKY_THUNDERSTORM;
    default:
        return WEATHER_SKY_UNKNOWN;
    }
}

weather_aq_band_t weather_aq_band(int aqi)
{
    return aqi <= 20    ? WEATHER_AQ_GOOD
           : aqi <= 40  ? WEATHER_AQ_FAIR
           : aqi <= 60  ? WEATHER_AQ_MODERATE
           : aqi <= 80  ? WEATHER_AQ_POOR
           : aqi <= 100 ? WEATHER_AQ_VERY_POOR
                        : WEATHER_AQ_EXTREMELY_POOR;
}

weather_uv_band_t weather_uv_band(int uv10)
{
    int uv = (uv10 + 5) / 10; /* the index is read rounded: 2.5 is 3 */
    return uv <= 2 ? WEATHER_UV_LOW : uv <= 5 ? WEATHER_UV_MODERATE : uv <= 7 ? WEATHER_UV_HIGH
                                                                    : uv <= 10 ? WEATHER_UV_VERY_HIGH : WEATHER_UV_EXTREME;
}

/* The season and peak thresholds in grains/m³ (EAACI, as CAMS uses them; spec §5.1). */
static void thresholds(ds_pollen_t type, int *season, int *peak)
{
    bool grass_like = type == DS_POLLEN_GRASS || type == DS_POLLEN_RAGWEED;
    *season = grass_like ? 3 : 10;
    *peak = grass_like ? 50 : 100;
}

weather_pollen_level_t weather_pollen_level(ds_pollen_t type, uint16_t grains10)
{
    int season, peak;
    thresholds(type, &season, &peak);
    if (grains10 == DS_POLLEN_NONE || grains10 < 10) {
        return WEATHER_POLLEN_NONE;
    }
    return grains10 >= peak * 10 ? WEATHER_POLLEN_HIGH : grains10 >= season * 10 ? WEATHER_POLLEN_MODERATE
                                                                                : WEATHER_POLLEN_LOW;
}

int weather_pollen_top(const uint16_t day[DS_POLLEN_TYPES])
{
    int best = -1;
    weather_pollen_level_t best_level = WEATHER_POLLEN_NONE;
    double best_share = 0;
    for (int p = 0; p < DS_POLLEN_TYPES; p++) {
        weather_pollen_level_t level = weather_pollen_level((ds_pollen_t)p, day[p]);
        if (level == WEATHER_POLLEN_NONE) {
            continue;
        }
        int season, peak;
        thresholds((ds_pollen_t)p, &season, &peak);
        double share = day[p] / (peak * 10.0);
        if (level > best_level || (level == best_level && share > best_share)) {
            best = p;
            best_level = level;
            best_share = share;
        }
    }
    return best;
}

const char *weather_pollen_name(ds_pollen_t type)
{
    static const char *const k_names[DS_POLLEN_TYPES] = { "alder", "birch", "grass", "mugwort", "olive", "ragweed" };
    return (unsigned)type < DS_POLLEN_TYPES ? k_names[type] : "";
}
