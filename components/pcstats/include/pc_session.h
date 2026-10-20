#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The PC session's rules (spec §12a.4, D44), pure: what a session switches when a sync finds the PC, when silence
 * means a reconnect and when the end, and what comes back then. The owner's hand wins: a mode or preset changed
 * by hand since the start stays. Presets go by id, so a presets save meanwhile doesn't move them. Host-buildable.
 */

#define PC_PRESET_ID_LEN 16 /* as UI_PRESET_ID_LEN */
#define PC_SESSION_ALWAYS 2 /* sync mode `always`: SETTINGS_SYNC_ALWAYS (main/app_pc.c checks) */

typedef struct {
    bool auto_always;               /* switch to sync mode always */
    char preset[PC_PRESET_ID_LEN]; /* show this preset; "" for none */
} pc_session_options_t;

typedef struct {
    bool active;
    bool set_always;  /* the session switched to always, from mode_before */
    int mode_before;
    char preset[PC_PRESET_ID_LEN];        /* the preset the session showed; "" for none */
    char preset_before[PC_PRESET_ID_LEN]; /* the one on screen before it; "" when it was the session's own */
    int64_t heard_ms;                     /* the last message, or the start */
    bool reconnecting;
    int64_t night_until;                  /* a schedule's night that came due meanwhile, until then (UTC); 0: none */
} pc_session_t;

typedef struct {
    bool to_always;
    char select[PC_PRESET_ID_LEN]; /* "" for none */
} pc_start_t;

typedef struct {
    bool restore_mode;
    int mode;                      /* when restore_mode */
    char select[PC_PRESET_ID_LEN]; /* "" for none */
} pc_end_t;

typedef enum { PC_STEADY, PC_RECONNECT, PC_END } pc_check_t;

void pc_session_init(pc_session_t *s);
/* Without switching to always, a session lasts only while Wi-Fi stays up anyway: in always. */
bool pc_session_may_start(const pc_session_options_t *o, bool in_always);
pc_start_t pc_session_start(pc_session_t *s, const pc_session_options_t *o, bool in_always, int mode,
                            const char *shown, int64_t now_ms);
void pc_session_heard(pc_session_t *s, int64_t now_ms);
/* After three intervals without a message (5 s at least): a reconnect, once; after 15 s, or twice the first
 * wait if that's longer: the end. */
pc_check_t pc_session_check(pc_session_t *s, uint32_t interval_ms, int64_t now_ms);
/* `still_always`: the sync mode is still always; `shown`: the preset on screen now. */
pc_end_t pc_session_end(pc_session_t *s, bool still_always, const char *shown);
bool pc_session_pauses_cycle(const pc_session_t *s, const char *shown);
/* Quiet hours and a night's schedule entry don't end a running session (spec §12a.4). */
bool pc_session_keeps_wifi(const pc_session_t *s);
/* What the files keep while a session runs, so a restart in the middle of one gives back what the owner had: the mode
 * from before while the mode is still the session's `always` (`mode` otherwise), and the preset from before while
 * the session's own is on screen (NULL: the one shown). */
int pc_session_saved_mode(const pc_session_t *s, int mode);
const char *pc_session_saved_preset(const pc_session_t *s, const char *shown);
/* A schedule's night that comes due during a session is kept for its end (the one that ends last, if two)... */
void pc_session_defer_night(pc_session_t *s, int64_t until);
/* ...which starts it while its own end is still to come: `until`, once, or 0. */
int64_t pc_session_take_night(pc_session_t *s, int64_t now);
