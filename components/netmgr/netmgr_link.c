#include "netmgr_link.h"

netmgr_disc_t netmgr_disconnected(uint8_t reason, bool joining, bool station)
{
    if (reason == NETMGR_REASON_ASSOC_LEAVE) {
        return NETMGR_DISC_IGNORE; /* join() leaves the old link first; its event may come late */
    }
    if (joining) {
        return NETMGR_DISC_FAILED;
    }
    return station ? NETMGR_DISC_RECONNECT : NETMGR_DISC_IGNORE;
}
