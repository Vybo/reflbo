#include "timekeeping_zones.h"

#include <string.h>

/* West to east from UTC, then the Americas. Rules as of 2026; angle-bracket names are avoided
 * because older newlib versions don't parse them. */
static const timekeeping_zone_t k_zones[] = {
    { "UTC", "UTC0" },
    { "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Moscow", "MSK-3" },
    { "Asia/Dubai", "GST-4" },
    { "Asia/Kolkata", "IST-5:30" },
    { "Asia/Shanghai", "CST-8" },
    { "Asia/Tokyo", "JST-9" },
    { "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "America/New_York", "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Chicago", "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Denver", "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0" },
};

const timekeeping_zone_t *timekeeping_zones(int *count)
{
    *count = (int)(sizeof(k_zones) / sizeof(k_zones[0]));
    return k_zones;
}

int timekeeping_zone_find(const char *iana)
{
    for (int i = 0; iana != NULL && i < (int)(sizeof(k_zones) / sizeof(k_zones[0])); i++) {
        if (strcmp(k_zones[i].iana, iana) == 0) {
            return i;
        }
    }
    return -1;
}
