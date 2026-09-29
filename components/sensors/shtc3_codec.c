#include "shtc3_codec.h"

uint8_t shtc3_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1);
        }
    }
    return crc;
}

bool shtc3_parse(const uint8_t raw[6], int *temp_c100, int *hum_pct100)
{
    if (shtc3_crc8(raw, 2) != raw[2] || shtc3_crc8(raw + 3, 2) != raw[5]) {
        return false;
    }
    int32_t t = (raw[0] << 8) | raw[1];
    int32_t rh = (raw[3] << 8) | raw[4];
    *temp_c100 = (int)(-4500 + (17500 * t + 32768) / 65536); /* T = -45 + 175 * S / 2^16 */
    *hum_pct100 = (int)((10000 * rh + 32768) / 65536);        /* RH = 100 * S / 2^16 */
    return true;
}

int shtc3_offset_humidity(int hum_pct100, int offset_pct100)
{
    int h = hum_pct100 + offset_pct100;
    return h < 0 ? 0 : h > 10000 ? 10000 : h;
}
