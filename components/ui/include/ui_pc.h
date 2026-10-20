#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pc_msg.h"
#include "ui_fields.h"

/*
 * The PC view (spec §12a, M7, D44): what the app gathers for the pc.* fields. Pure C, host-buildable.
 */

#define UI_PC_FRESH_INTERVALS 3 /* a reading is fresh for three intervals after its message (spec §5.1) */

struct ui_pc {
    bool session;                /* a session runs (spec §12a.4); without one every pc.* field is missing */
    const pc_catalog_t *catalog; /* the session's catalogue, or the cached one (/fs/state/pc.json); NULL for none */
    const pc_values_t *values;   /* the last readings message's values, by the catalogue's order; NULL before one */
    uint32_t age_ms;             /* since that message */
    uint32_t interval_ms;        /* the session's interval */
};
