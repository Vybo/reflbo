#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "netmgr_list.h"
#include "netmgr_scan.h"

/*
 * The Wi-Fi manager (spec §10.1, §10.2, §9.3). In config mode netmgr_start() joins a saved network,
 * or starts the device's own AP with a captive portal when none is saved or none answers; a sync and
 * sync mode `always` join a saved network with netmgr_join() and never start the AP. It runs in its own task; the calls return at once, except netmgr_scan(), which waits
 * for its result. Any task may call them.
 */

typedef enum {
    NETMGR_OFF,
    NETMGR_JOINING, /* trying the saved networks */
    NETMGR_STATION, /* on a network: `ip` is the device's address there */
    NETMGR_AP,      /* only the device's own AP runs */
} netmgr_state_t;

typedef enum {
    NETMGR_TEST_NONE,    /* no test since Wi-Fi came on */
    NETMGR_TEST_RUNNING,
    NETMGR_TEST_OK,
    NETMGR_TEST_WRONG_PASSWORD,
    NETMGR_TEST_NOT_FOUND,
    NETMGR_TEST_FAILED,  /* anything else: no address, a timeout */
    NETMGR_TEST_INVALID, /* netmgr_net_valid() refused it */
} netmgr_test_t;

typedef struct {
    netmgr_state_t state;
    bool ap_on;                    /* the AP runs: the AP state, or beside the station (keep_ap, a test) */
    char ssid[NETMGR_SSID_MAX + 1]; /* the network joined */
    char ip[16];                   /* on that network */
    int8_t rssi;
    uint8_t ap_clients;
    char ap_ssid[NETMGR_SSID_MAX + 1]; /* reflbo-XXXX */
    char ap_pass[12];
    char host[16]; /* reflbo-XXXX: the mDNS name without .local */
    netmgr_test_t test; /* the web UI's last test, and of which network */
    char test_ssid[NETMGR_SSID_MAX + 1];
} netmgr_status_t;

#define NETMGR_AP_IP "192.168.4.1"
#define NETMGR_SCAN_MAX 20


/* Reads the saved networks and the AP password (generated at the first boot) from NVS. Call
 * once, after nvs_flash_init(); a second call returns ESP_ERR_INVALID_STATE. `changed` runs on
 * the netmgr task or the event loop after every state change, and must not block. */
esp_err_t netmgr_init(void (*changed)(void));
/* Config mode starts: a saved network, else the AP. `keep_ap` keeps the AP up beside a station,
 * while no web password is set (D18). */
void netmgr_start(bool keep_ap);
void netmgr_stop(void); /* Wi-Fi off */
/* A saved network for a sync or sync mode `always` (spec §9.3), never the AP. Waits for the result:
 * ESP_OK once on a network (at once if already), ESP_ERR_NOT_FOUND with none saved, ESP_FAIL if none
 * joined (Wi-Fi is off again), ESP_ERR_INVALID_STATE while only the AP runs. Not from the app task,
 * which netmgr's callbacks reach. */
esp_err_t netmgr_join(void);
/* Config mode ended while a sync or `always` keeps the station: the AP goes, the station stays; with
 * no station, Wi-Fi goes off. */
void netmgr_ap_off(void);
void netmgr_status(netmgr_status_t *out);
/* Visible networks, strongest first, one entry per SSID; returns how many (up to `max`). */
int netmgr_scan(netmgr_ap_t *out, int max);
/* The web UI's "test" (spec §10.2): joins a new network beside the AP, found by a scan first so
 * a network out of reach never takes the AP off its channel. On success it is saved first in the
 * list and the device stays on it; on failure it goes back to the network it was on. Returns at
 * once: netmgr_status() reports the result. ESP_ERR_INVALID_ARG if netmgr_net_valid() refuses
 * the network, ESP_ERR_INVALID_STATE while Wi-Fi is off or a test runs. */
esp_err_t netmgr_test_start(const char *ssid, const char *pass);
/* Saves a network without trying it, as it may be out of range now; ESP_ERR_INVALID_ARG if
 * netmgr_net_valid() refuses it. */
esp_err_t netmgr_add(const char *ssid, const char *pass);
void netmgr_networks(netmgr_list_t *out); /* the saved list; the passwords are left out */
esp_err_t netmgr_forget(const char *ssid); /* ESP_ERR_NOT_FOUND if it isn't saved */
esp_err_t netmgr_forget_all(void);         /* Menu ▸ Wi-Fi ▸ Forget networks */
const char *netmgr_test_name(netmgr_test_t result);
