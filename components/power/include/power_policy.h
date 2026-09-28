#pragma once

#include <stdbool.h>

/* Sleep policy (spec §3.4, §9.1): whether and how to sleep now. Pure C, host-buildable. */

typedef enum {
    POWER_IDLE_LIGHT,
    POWER_IDLE_DEEP,
} power_idle_t;

typedef enum {
    POWER_PLAN_AWAKE, /* keep running; wait for events */
    POWER_PLAN_LIGHT,
    POWER_PLAN_DEEP,
    POWER_PLAN_RETRY, /* boot failed and no PC is attached: sleep a while, then boot from scratch */
} power_plan_t;

typedef struct {
    power_idle_t strategy;  /* the configured idle strategy */
    bool tethered;          /* a USB host is sending frames: sleeping would drop the console */
    bool hold_awake;        /* grace period after boot or a button, pending work, a gesture in progress */
    int test_cycles;        /* > 0: `sleep test` cycles left; they sleep even when tethered */
    power_idle_t test_mode;
    bool boot_failed;       /* the app could not start: there is no schedule to sleep on */
} power_policy_input_t;

power_plan_t power_policy(const power_policy_input_t *in);
