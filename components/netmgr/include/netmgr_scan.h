#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "netmgr_list.h"

/* What a Wi-Fi scan saw, and the choices made from it. Pure C, host-buildable. */

typedef struct {
    char ssid[NETMGR_SSID_MAX + 1]; /* "" for a hidden network */
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    bool open;
} netmgr_seen_t;

/* A network for the web UI's list. */
typedef struct {
    char ssid[NETMGR_SSID_MAX + 1];
    int8_t rssi;
    bool open;
} netmgr_ap_t;

/* The strongest access point named `ssid`, as an index into `seen`; -1 if none. */
int netmgr_seen_best(const netmgr_seen_t *seen, int n, const char *ssid);
/* One entry per name, in the order seen (the driver lists the strongest first), hidden networks
 * left out. Returns how many went into `out`. */
int netmgr_seen_unique(const netmgr_seen_t *seen, int n, netmgr_ap_t *out, int max);
/* The channel for the device's own AP. The station shares the radio, so joining a network on
 * another channel moves the AP and drops the phone on it: start on the channel of the strongest
 * saved network in sight, else of the strongest network, else 1. `saved` may be NULL. */
uint8_t netmgr_ap_channel(const netmgr_seen_t *seen, int n, const netmgr_list_t *saved);
