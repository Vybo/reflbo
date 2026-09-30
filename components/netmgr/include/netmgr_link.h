#pragma once

#include <stdbool.h>
#include <stdint.h>

/* What a station disconnection means (spec §10.1). Pure C, host-buildable. */

#define NETMGR_REASON_ASSOC_LEAVE 8 /* WIFI_REASON_ASSOC_LEAVE: our own esp_wifi_disconnect() */

typedef enum {
    NETMGR_DISC_IGNORE,    /* a link we dropped ourselves, or no network was joined */
    NETMGR_DISC_FAILED,    /* the attempt join() waits for failed */
    NETMGR_DISC_RECONNECT, /* the joined network went away: try it again */
} netmgr_disc_t;

/* `joining`: join() waits for an attempt; `station`: a network was joined. */
netmgr_disc_t netmgr_disconnected(uint8_t reason, bool joining, bool station);
