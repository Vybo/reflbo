#include "pc_session.h"

#include <stdio.h>
#include <string.h>

#define QUIET_AT_LEAST_MS 5000
#define END_AT_LEAST_MS 15000

static void copy_id(char *out, const char *id)
{
    snprintf(out, PC_PRESET_ID_LEN, "%s", id != NULL ? id : "");
}

void pc_session_init(pc_session_t *s)
{
    memset(s, 0, sizeof(*s));
}

bool pc_session_may_start(const pc_session_options_t *o, bool in_always)
{
    return o->auto_always || in_always;
}

pc_start_t pc_session_start(pc_session_t *s, const pc_session_options_t *o, bool in_always, int mode,
                            const char *shown, int64_t now_ms)
{
    pc_start_t a = { 0 };
    pc_session_init(s);
    s->active = true;
    s->heard_ms = now_ms;
    s->mode_before = mode;
    if (o->auto_always && !in_always) {
        a.to_always = s->set_always = true;
    }
    copy_id(s->preset, o->preset);
    if (s->preset[0] != '\0' && strcmp(s->preset, shown) != 0) {
        copy_id(a.select, s->preset);
        copy_id(s->preset_before, shown);
    }
    return a;
}

void pc_session_heard(pc_session_t *s, int64_t now_ms)
{
    s->heard_ms = now_ms;
    s->reconnecting = false;
}

pc_check_t pc_session_check(pc_session_t *s, uint32_t interval_ms, int64_t now_ms)
{
    if (!s->active) {
        return PC_STEADY;
    }
    int64_t quiet = (int64_t)interval_ms * 3;
    quiet = quiet > QUIET_AT_LEAST_MS ? quiet : QUIET_AT_LEAST_MS;
    int64_t end = quiet * 2 > END_AT_LEAST_MS ? quiet * 2 : END_AT_LEAST_MS;
    int64_t silent = now_ms - s->heard_ms;
    if (silent > end) {
        return PC_END;
    }
    if (silent > quiet && !s->reconnecting) {
        s->reconnecting = true;
        return PC_RECONNECT;
    }
    return PC_STEADY;
}

pc_end_t pc_session_end(pc_session_t *s, bool still_always, const char *shown)
{
    pc_end_t e = { 0 };
    if (!s->active) {
        return e;
    }
    if (s->set_always && still_always) {
        e.restore_mode = true;
        e.mode = s->mode_before;
    }
    if (s->preset_before[0] != '\0' && strcmp(shown, s->preset) == 0) {
        copy_id(e.select, s->preset_before);
    }
    pc_session_init(s);
    return e;
}

bool pc_session_pauses_cycle(const pc_session_t *s, const char *shown)
{
    return s->active && s->preset[0] != '\0' && strcmp(s->preset, shown) == 0;
}

bool pc_session_keeps_wifi(const pc_session_t *s)
{
    return s->active;
}

bool pc_session_left_always(const pc_session_t *s, int mode)
{
    return s->active && mode != PC_SESSION_ALWAYS;
}

int pc_session_saved_mode(const pc_session_t *s, int mode)
{
    return s->active && s->set_always && mode == PC_SESSION_ALWAYS ? s->mode_before : mode;
}

const char *pc_session_saved_preset(const pc_session_t *s, const char *shown)
{
    return s->active && s->preset_before[0] != '\0' && strcmp(shown, s->preset) == 0 ? s->preset_before : NULL;
}

void pc_session_defer_night(pc_session_t *s, int64_t until)
{
    if (s->active && until > s->night_until) {
        s->night_until = until;
    }
}

int64_t pc_session_take_night(pc_session_t *s, int64_t now)
{
    int64_t until = s->night_until > now ? s->night_until : 0;
    s->night_until = 0;
    return until;
}
