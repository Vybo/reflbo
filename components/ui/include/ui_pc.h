#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "gfx.h"
#include "pc_msg.h"
#include "ui_fields.h"
#include "ui_history.h"

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
    char name[32];               /* the PC's, from /v1/info; "" before one */
    time_t since;                /* when the last session ended (UTC), for "PC asleep"; 0: none since the boot */
};

/* "since 18:12" (D43's forms: today the time, earlier in the week "Mon 18:12", else the date), in the language. */
void ui_pc_since_text(const ui_context_t *ctx, time_t since, char *out, size_t size);
/* The PC layout's "PC asleep" (spec §5.2): a box across it with since when and the PC's name (ui_dashboard.c). */
void ui_pc_draw_asleep(gfx_fb_t *fb, const ui_context_t *ctx);
/* The sample PC (spec §12a.4, `pc demo`): the spike's seven readings and their latest values. */
void ui_pc_demo(pc_catalog_t *cat, pc_values_t *vals);
/* And their last minute, at 1 s, into the series `h` keeps for them, ending at `now_ms`. */
void ui_pc_demo_history(ui_history_t *h, uint32_t now_ms);
