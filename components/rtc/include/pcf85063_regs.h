#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * PCF85063A register codec (datasheet Rev. 6, §8): time and alarm registers <-> UTC seconds. The
 * RTC stores UTC (spec §7). Pure C, host-buildable; the I²C transfers live in rtc.c.
 */

#define PCF85063_ADDR           0x51
#define PCF85063_REG_CONTROL_1  0x00
#define PCF85063_REG_CONTROL_2  0x01
#define PCF85063_REG_SECONDS    0x04 /* 04h-0Ah: seconds .. years, read and written in one transfer */
#define PCF85063_REG_ALARM      0x0B /* 0Bh-0Fh: second, minute, hour, day, weekday alarms */
#define PCF85063_REG_TIMER_MODE 0x11

#define PCF85063_TIME_LEN  7
#define PCF85063_ALARM_LEN 5

/* Control_1: 24-hour mode, CAP_SEL = 7 pF (the vendor default), clock running. */
#define PCF85063_CONTROL_1_RUN 0x00
/* Control_2: AIE on, AF = 0 (clears the alarm flag; flag writes are ANDed), MI/HMI off,
 * TF = 1 (left as is), COF = 111 (CLKOUT off; it is on after power-up). */
#define PCF85063_CONTROL_2_RUN 0x8F
#define PCF85063_CONTROL_2_AF  0x40
/* Timer_mode: timer off, clock 1/60 Hz, the low-power setting (Table 36). */
#define PCF85063_TIMER_MODE_OFF 0x18

/* 2000-01-01T00:00:00Z .. 2099-12-31T23:59:59Z */
#define PCF85063_MIN_TIME ((time_t)946684800)
#define PCF85063_MAX_TIME ((time_t)4102444799)

/* Decodes 04h-0Ah. `stopped` reports the oscillator-stop flag (time not trustworthy). False if a
 * register holds an impossible value. */
bool pcf85063_decode_time(const uint8_t regs[PCF85063_TIME_LEN], time_t *utc, bool *stopped);
/* Encodes 04h-0Ah with the oscillator-stop flag cleared. `utc` is clamped to the RTC's range. */
void pcf85063_encode_time(time_t utc, uint8_t regs[PCF85063_TIME_LEN]);
/* Encodes 0Bh-0Fh for an alarm at `wake` (a whole minute, within the next 28 days): minute, hour
 * and day enabled, second and weekday disabled. AF sets when the time reaches it. */
void pcf85063_encode_alarm(time_t wake, uint8_t regs[PCF85063_ALARM_LEN]);
