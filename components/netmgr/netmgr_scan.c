#include "netmgr_scan.h"

#include <stdio.h>
#include <string.h>

#define DEFAULT_CHANNEL 1

int netmgr_seen_best(const netmgr_seen_t *seen, int n, const char *ssid)
{
    int best = -1;
    for (int i = 0; ssid != NULL && ssid[0] != '\0' && i < n; i++) {
        if (strcmp(seen[i].ssid, ssid) == 0 && (best < 0 || seen[i].rssi > seen[best].rssi)) {
            best = i;
        }
    }
    return best;
}

int netmgr_seen_unique(const netmgr_seen_t *seen, int n, netmgr_ap_t *out, int max)
{
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        bool dup = seen[i].ssid[0] == '\0';
        for (int j = 0; j < count && !dup; j++) {
            dup = strcmp(out[j].ssid, seen[i].ssid) == 0;
        }
        if (!dup) {
            snprintf(out[count].ssid, sizeof(out[count].ssid), "%s", seen[i].ssid);
            out[count].rssi = seen[i].rssi;
            out[count].open = seen[i].open;
            count++;
        }
    }
    return count;
}

uint8_t netmgr_ap_channel(const netmgr_seen_t *seen, int n, const netmgr_list_t *saved)
{
    int best = -1;
    for (int s = 0; saved != NULL && s < saved->count; s++) {
        int i = netmgr_seen_best(seen, n, saved->nets[s].ssid);
        if (i >= 0 && (best < 0 || seen[i].rssi > seen[best].rssi)) {
            best = i;
        }
    }
    for (int i = 0; best < 0 && i < n; i++) {
        if (seen[i].ssid[0] != '\0' && (best < 0 || seen[i].rssi > seen[best].rssi)) {
            best = i;
        }
    }
    return best >= 0 && seen[best].channel >= 1 && seen[best].channel <= 13 ? seen[best].channel : DEFAULT_CHANNEL;
}
