#include "power_policy.h"

static power_plan_t plan_for(power_idle_t idle)
{
    return idle == POWER_IDLE_DEEP ? POWER_PLAN_DEEP : POWER_PLAN_LIGHT;
}

power_plan_t power_policy(const power_policy_input_t *in)
{
    if (in->boot_failed && in->image_pending) {
        return POWER_PLAN_ROLLBACK; /* before hold_awake: a pending image holds the board awake itself */
    }
    if (in->hold_awake) {
        return POWER_PLAN_AWAKE;
    }
    if (in->boot_failed) {
        return in->tethered ? POWER_PLAN_AWAKE : POWER_PLAN_RETRY; /* the console is the only use left */
    }
    if (in->test_cycles > 0) {
        return plan_for(in->test_mode);
    }
    if (in->tethered) {
        return POWER_PLAN_AWAKE;
    }
    return plan_for(in->strategy);
}
