#include "sync_ntp.h"

#include <string.h>

#define NTP_UNIX_DELTA 2208988800LL /* 1900-01-01 to 1970-01-01, in seconds */
#define LI_VN_MODE_CLIENT 0x23     /* leap 0, version 4, mode 3 */

/* UTC µs <-> the 64-bit NTP timestamp (seconds since 1900 and a 32-bit fraction). After 2036 the
 * seconds wrap; RFC 4330 §3: a timestamp with its top bit clear is in the next era. */
static uint64_t to_ntp(int64_t utc_us)
{
    int64_t s = utc_us / 1000000 + NTP_UNIX_DELTA;
    int64_t us = utc_us % 1000000;
    return ((uint64_t)(uint32_t)s << 32) | (uint32_t)(((uint64_t)us << 32) / 1000000);
}

static int64_t from_ntp(uint64_t ntp)
{
    uint32_t s = (uint32_t)(ntp >> 32);
    int64_t seconds = (int64_t)s + ((s & 0x80000000u) ? 0 : (1LL << 32)) - NTP_UNIX_DELTA;
    int64_t us = (int64_t)((((uint64_t)(uint32_t)ntp) * 1000000 + 0x80000000u) >> 32);
    return seconds * 1000000 + us;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (56 - 8 * i));
    }
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = v << 8 | p[i];
    }
    return v;
}

void sync_ntp_request(uint8_t out[SYNC_NTP_PACKET], int64_t t1_us)
{
    memset(out, 0, SYNC_NTP_PACKET);
    out[0] = LI_VN_MODE_CLIENT;
    put64(out + 40, to_ntp(t1_us));
}

bool sync_ntp_offset(const uint8_t *reply, size_t len, int64_t t1_us, int64_t t4_us, int64_t *offset_us,
                     int64_t *delay_us)
{
    if (len < SYNC_NTP_PACKET) {
        return false;
    }
    int leap = reply[0] >> 6, mode = reply[0] & 7, stratum = reply[1];
    uint64_t originate = get64(reply + 24), receive = get64(reply + 32), transmit = get64(reply + 40);
    if (mode != 4 || leap == 3 || stratum == 0 || stratum > 15 || transmit == 0 || originate != to_ntp(t1_us)) {
        return false;
    }
    int64_t t2 = from_ntp(receive), t3 = from_ntp(transmit);
    *offset_us = ((t2 - t1_us) + (t3 - t4_us)) / 2;
    *delay_us = (t4_us - t1_us) - (t3 - t2);
    return true;
}
