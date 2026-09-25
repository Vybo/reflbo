#pragma once

#include <stdint.h>

/* Proleptic Gregorian calendar <-> days since 1970-01-01 (Howard Hinnant's algorithms). Pure C. */
int64_t util_days_from_civil(int year, int month, int day);
void util_civil_from_days(int64_t days, int *year, int *month, int *day);
