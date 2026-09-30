#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Saved Wi-Fi networks (spec §10.1): up to 5, most recently successful first, as they are tried.
 * Each keeps the BSSID and channel of its last success for a fast connect. Pure C; netmgr keeps
 * the list in NVS (namespace `wifi`).
 */

#define NETMGR_LIST_MAX 5
#define NETMGR_SSID_MAX 32 /* bytes (802.11) */
#define NETMGR_PASS_MAX 64 /* a WPA2 passphrase of 8-63 characters, or a 64-digit hex key */

typedef struct {
    char ssid[NETMGR_SSID_MAX + 1];
    char pass[NETMGR_PASS_MAX + 1]; /* empty: an open network */
    uint8_t bssid[6];               /* all zero until a connection succeeds */
    uint8_t channel;
} netmgr_net_t;

typedef struct {
    uint8_t count;
    netmgr_net_t nets[NETMGR_LIST_MAX];
} netmgr_list_t;

/* An SSID of 1-32 bytes, and a password that is empty, 8-63 printable ASCII characters, or 64 hex
 * digits. */
bool netmgr_net_valid(const char *ssid, const char *pass);
int netmgr_list_find(const netmgr_list_t *l, const char *ssid); /* index, or -1 */
/* Adds a network, or replaces the password of one with the same SSID, and puts it first; a full
 * list drops its last. False if netmgr_net_valid() refuses it. */
bool netmgr_list_add(netmgr_list_t *l, const char *ssid, const char *pass);
/* The network at `index` connected: it moves to the front, with where it was found. */
void netmgr_list_succeeded(netmgr_list_t *l, int index, const uint8_t bssid[6], uint8_t channel);
bool netmgr_list_forget(netmgr_list_t *l, const char *ssid);
