#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SHTC3 data handling (datasheet §5.6, §5.9, §5.11): CRC and conversion. Pure C, host-buildable. */

uint8_t shtc3_crc8(const uint8_t *data, size_t len); /* poly 0x31, init 0xFF */
/* Parses a T-first measurement: T msb, lsb, crc, RH msb, lsb, crc. False on a CRC mismatch. */
bool shtc3_parse(const uint8_t raw[6], int *temp_c100, int *hum_pct100);
