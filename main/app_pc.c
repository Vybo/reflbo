#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_internal.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pc_session.h"
#include "pc_ws.h"
#include "storage.h"
#include "ui_history.h"

/* The PC's sensors on the device (spec §12a, M7, D44): the session a sync's PC step starts, the stream's messages,
 * the catalogue kept in /fs/state/pc.json, and the charts' history. All of it belongs to the app task. */

static const char *TAG = "app_pc";

#define CACHE_PATH "/fs/state/pc.json"
#define CHECK_MS 1000 /* how often a session looks at its silence */

_Static_assert(PC_SESSION_ALWAYS == SETTINGS_SYNC_ALWAYS, "pc_session's `always` is the settings'");

static pc_session_t s_sess;
static pc_catalog_t s_cat; /* the session's catalogue, or the cached one */
static bool s_cat_known;
static pc_values_t s_vals; /* the last readings message's */
static bool s_have_vals;
static int64_t s_heard_ms; /* when it came */
static pc_hwinfo_t s_hw;
static pc_info_t s_info; /* the last /v1/info */
static time_t s_since;   /* when the last session ended (UTC) */
static uint32_t s_gen;   /* the open stream's events */
static bool s_connected;
static char s_why[64]; /* why the stream last closed, or a frame was refused */
static bool s_demo, s_demo_filled;
static bool s_cache_tried; /* a routine wake mounts no storage: the file is read once it is up */
static struct {
    bool enabled;
    char host[SETTINGS_HOST_LEN];
    uint16_t port, interval_ms;
} s_seen; /* the settings the session runs with */
static ui_pc_t s_view;
static ui_history_t s_hist;
static ui_series_t *s_series; /* PSRAM, from the first chart */

typedef pc_ws_msg_t ws_event_t; /* a copy, posted to the app task */

/* ---- the history ---- */

/* The series of the charts every preset shows (spec §5.3): kept, the rest freed; the memory from the first. */
void app_pc_presets_changed(void)
{
    char names[UI_HIST_SERIES][UI_HIST_NAME_LEN];
    const char *list[UI_HIST_SERIES];
    int n = ui_presets_chart_fields(app_presets(), names);
    if (n == 0 && s_series == NULL) {
        return;
    }
    if (s_series == NULL) {
        s_series = heap_caps_calloc(UI_HIST_SERIES, sizeof(ui_series_t), MALLOC_CAP_SPIRAM);
        if (s_series == NULL) {
            ESP_LOGE(TAG, "no PSRAM for the charts' history");
            return;
        }
        ui_history_init(&s_hist, s_series);
    }
    for (int i = 0; i < n; i++) {
        list[i] = names[i];
    }
    ESP_LOGI(TAG, "history: %d series", ui_history_track(&s_hist, list, n));
}

static bool is_pc_name(const char *name)
{
    return strncmp(name, "pc.", 3) == 0;
}

/* The PC's readings, each series from the message just taken. */
static void feed_pc(uint32_t now_ms)
{
    for (int i = 0; s_series != NULL && i < UI_HIST_SERIES; i++) {
        ui_series_t *s = &s_series[i];
        if (!s->name[0] || !is_pc_name(s->name)) {
            continue;
        }
        int k = pc_catalog_find(&s_cat, s->name + 3);
        if (k >= 0 && s_vals.has[k]) {
            ui_series_push(s, now_ms, s_vals.v[k]);
        } else {
            ui_series_break(s, now_ms); /* HWiNFO sends none: the line stops */
        }
    }
}

void app_pc_feed(bool sample)
{
    if (s_series == NULL) {
        return;
    }
    ui_context_t ctx;
    app_ui_context(&ctx);
    for (int i = 0; i < UI_HIST_SERIES; i++) {
        ui_series_t *s = &s_series[i];
        bool local = strncmp(s->name, "env.", 4) == 0 || strncmp(s->name, "bat.", 4) == 0;
        if (!s->name[0] || is_pc_name(s->name) || local != sample) { /* each at its source's own rate */
            continue;
        }
        ui_value_t v;
        ui_resolve(&ctx, ui_field_by_name(s->name), &v);
        if (v.has_num && v.state == UI_VALUE_FRESH) {
            ui_series_push(s, ctx.now_ms, v.num);
        }
    }
}

/* ---- the catalogue's file ---- */

static bool parse_cache(const char *text, void *ctx)
{
    (void)ctx;
    pc_hwinfo_t hw;
    char err[64];
    return pc_parse(text, strlen(text), &s_cat, &s_vals, &hw, err, sizeof(err)) == PC_MSG_CATALOG;
}

static void save_cache(void)
{
    EXT_RAM_BSS_ATTR static char text[PC_FRAME_MAX];
    size_t n = pc_catalog_json(&s_cat, text, sizeof(text));
    esp_err_t err = n > 0 && storage_ready() ? storage_write_atomic(CACHE_PATH, text, n) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: %s", CACHE_PATH, esp_err_to_name(err));
    }
}

/* The last catalogue from its file, so the editor lists the PC's fields while the PC is off (spec §12a.3). */
static void load_cache(void)
{
    EXT_RAM_BSS_ATTR static char text[PC_FRAME_MAX + 1];
    s_cache_tried = storage_ready();
    bool from_backup = false;
    s_cat_known = storage_ready() && storage_load(CACHE_PATH, text, sizeof(text), parse_cache, NULL,
                                                  &from_backup) == ESP_OK;
    if (s_cat_known) {
        ESP_LOGI(TAG, "catalogue: %d readings from %s", s_cat.count, CACHE_PATH);
    }
}

void app_pc_init(void)
{
    pc_session_init(&s_sess);
    load_cache(); /* the history's memory waits until the board stays awake (app_pc_presets_changed()) */
}

/* ---- the session ---- */

static const char *shown_id(void)
{
    const ui_presets_t *p = app_presets();
    return p->presets[p->active].id;
}

static bool shows_pc(void)
{
    const ui_presets_t *p = app_presets();
    const ui_preset_t *active = &p->presets[p->active];
    for (int i = 0; i < ui_preset_slots(active); i++) {
        if (ui_field_is_pc((ui_field_id_t)active->slots[i])) {
            return true;
        }
    }
    return false;
}

static void set_mode(uint8_t mode)
{
    settings_t *set = &app_state()->settings;
    if (mode == SETTINGS_SYNC_ALWAYS) {
        settings_toggle_always(set); /* as BOOT double does: the mode before is kept */
    } else {
        set->sync_mode = mode;
    }
    app_ui_save_settings();
    app_clock_moved(0); /* `always` takes Wi-Fi at once; leaving it, it goes */
}

static void select_id(const char *id)
{
    int i = id[0] ? ui_presets_find(app_presets(), id) : -1;
    if (i >= 0) {
        app_ui_select(i, false); /* the session's switch isn't saved, as an auto-cycle switch isn't */
    }
}

static void on_ws(const pc_ws_msg_t *msg); /* on the stream's task */

static void open_stream(void)
{
    const settings_t *set = app_settings();
    char token[SETTINGS_SECRET_LEN];
    app_secret_get(SETTINGS_SECRET_PC_TOKEN, token, sizeof(token));
    s_connected = false;
    s_gen = 0; /* the last stream's events, which may still come, are dropped */
    esp_err_t err = pc_ws_open(set->pc_host, set->pc_port, token, on_ws, &s_gen);
    if (err != ESP_OK) {
        snprintf(s_why, sizeof(s_why), "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "the stream: %s", s_why);
    }
}

static void end_session(const char *why)
{
    if (!s_sess.active) {
        return;
    }
    const settings_t *set = app_settings();
    int64_t night = pc_session_take_night(&s_sess, time(NULL)); /* a schedule's night that came meanwhile */
    pc_end_t e = pc_session_end(&s_sess, set->sync_mode == SETTINGS_SYNC_ALWAYS, shown_id());
    pc_ws_close();
    s_gen = 0;
    s_connected = false;
    s_have_vals = false;
    s_since = time(NULL);
    uint32_t now_ms = (uint32_t)app_uptime_ms();
    for (int i = 0; s_series != NULL && i < UI_HIST_SERIES; i++) {
        if (s_series[i].name[0] && is_pc_name(s_series[i].name)) {
            ui_series_break(&s_series[i], now_ms); /* the next session's line starts apart */
        }
    }
    ESP_LOGI(TAG, "session ends: %s%s%s", why, e.restore_mode ? ", the mode back" : "",
             e.select[0] ? ", the preset back" : "");
    if (e.restore_mode) {
        set_mode((uint8_t)e.mode);
    }
    if (e.select[0]) {
        select_id(e.select);
    } else {
        app_ui_render();
    }
    if (night != 0) {
        app_ui_begin_night((time_t)night); /* spec §12a.4: it waited for the session */
    }
}

static void start_session(void)
{
    const settings_t *set = app_settings();
    pc_session_options_t o = { .auto_always = set->pc_auto_always };
    snprintf(o.preset, sizeof(o.preset), "%s", set->pc_preset);
    bool always = set->sync_mode == SETTINGS_SYNC_ALWAYS;
    if (!pc_session_may_start(&o, always)) {
        ESP_LOGI(TAG, "%s found; a session needs sync mode always", s_info.name);
        return;
    }
    pc_start_t a = pc_session_start(&s_sess, &o, always, set->sync_mode, shown_id(), app_uptime_ms());
    s_seen.enabled = set->pc_enabled;
    snprintf(s_seen.host, sizeof(s_seen.host), "%s", set->pc_host);
    s_seen.port = set->pc_port;
    s_seen.interval_ms = set->pc_interval_ms;
    s_hw = s_info.hwinfo;
    s_why[0] = '\0';
    s_demo = false;
    ESP_LOGI(TAG, "session starts with %s%s%s", s_info.name, a.to_always ? ", sync mode always" : "",
             a.select[0] ? ", its preset" : "");
    if (a.to_always) {
        set_mode(SETTINGS_SYNC_ALWAYS);
    }
    open_stream();
    if (a.select[0]) {
        select_id(a.select);
    } else {
        app_ui_render();
    }
}

void app_pc_report(const sync_report_t *r)
{
    if (r->result[SYNC_STEP_PC] != SYNC_STEP_OK || app_state()->critical) {
        return;
    }
    s_info = r->pc;
    if (!s_sess.active) {
        start_session();
    }
}

static void hello(void)
{
    char text[96];
    size_t n = pc_hello(text, sizeof(text), app_settings()->pc_interval_ms);
    if (n > 0 && pc_ws_send(text, n) != ESP_OK) {
        ESP_LOGW(TAG, "hello not sent");
    }
}

/* A frame, whole (spec §12a.3). */
static void take_frame(char *frame, size_t len)
{
    char err[64];
    pc_msg_kind_t kind = pc_parse(frame, len, &s_cat, &s_vals, &s_hw, err, sizeof(err));
    int64_t now_ms = app_uptime_ms();
    if (kind == PC_MSG_INVALID) {
        snprintf(s_why, sizeof(s_why), "%s", err);
        ESP_LOGW(TAG, "a frame refused: %s; closing", err);
        pc_ws_close(); /* the silence brings the reconnect */
        s_gen = 0;
        s_connected = false;
        return;
    }
    pc_session_heard(&s_sess, now_ms);
    if (kind == PC_MSG_CATALOG) {
        s_cat_known = true;
        s_have_vals = false; /* the values follow the catalogue's order */
        save_cache();
        ESP_LOGI(TAG, "catalogue: %d readings", s_cat.count);
        if (ui_presets_fill_pc(app_presets(), &s_cat)) { /* the built-in PC preset's first readings */
            app_ui_save_presets();
            app_pc_presets_changed();
        }
        app_ui_render();
    } else if (kind == PC_MSG_READINGS) {
        s_have_vals = true;
        s_heard_ms = now_ms;
        feed_pc((uint32_t)now_ms);
        if (shows_pc()) {
            app_ui_render(); /* spec §12a.4: a redraw on each message */
        }
    } else if (kind == PC_MSG_STATUS) {
        ESP_LOGI(TAG, "HWiNFO: %s", pc_hwinfo_name(s_hw));
        app_ui_render();
    }
}

static void apply_ws(void *arg) /* on the app task */
{
    ws_event_t *e = arg;
    if (e->gen == s_gen && s_sess.active) {
        switch (e->ev) {
        case PC_WS_CONNECTED:
            s_connected = true;
            hello();
            break;
        case PC_WS_FRAME:
            take_frame(e->frame, e->len);
            break;
        case PC_WS_TOO_BIG:
            snprintf(s_why, sizeof(s_why), "a frame over 8 KB");
            ESP_LOGW(TAG, "%s; closing", s_why);
            pc_ws_close();
            s_gen = 0;
            s_connected = false;
            break;
        case PC_WS_CLOSED: {
            char why[48];
            bool end = pc_close_action(e->close_code, e->reason, e->http_status, why, sizeof(why)) == PC_CLOSE_END;
            snprintf(s_why, sizeof(s_why), "%s", why);
            ESP_LOGI(TAG, "the stream closed: %s", why);
            s_connected = false;
            if (end) { /* the app quit or turned the board away: at once, not after the silence (spec §12a.3) */
                end_session(why);
            }
            break;
        }
        }
    }
    free(e->frame);
    free(e);
}

static void on_ws(const pc_ws_msg_t *msg)
{
    ws_event_t *e = malloc(sizeof(*e));
    if (e == NULL) {
        free(msg->frame);
        return;
    }
    *e = *msg;
    if (app_post(apply_ws, e) != ESP_OK) { /* the app is busy: the next message comes within the interval */
        free(e->frame);
        free(e);
    }
}

void app_pc_tick(void)
{
    if (!s_cache_tried && !s_cat_known && storage_ready()) {
        load_cache(); /* after a warm wake, for the editor's PC fields */
    }
    if (!s_sess.active) {
        return;
    }
    const settings_t *set = app_settings();
    if (!set->pc_enabled || strcmp(set->pc_host, s_seen.host) != 0 || set->pc_port != s_seen.port) {
        end_session(set->pc_enabled ? "a new address" : "PC stats off");
        return;
    }
    if (set->pc_interval_ms != s_seen.interval_ms) {
        s_seen.interval_ms = set->pc_interval_ms;
        hello(); /* spec §12a.3: sent again when the interval changes */
    }
    switch (pc_session_check(&s_sess, set->pc_interval_ms, app_uptime_ms())) {
    case PC_RECONNECT:
        ESP_LOGI(TAG, "silence: reconnecting");
        open_stream();
        break;
    case PC_END:
        end_session("silence");
        break;
    default:
        break;
    }
}

int64_t app_pc_deadline_ms(void)
{
    return s_sess.active ? app_uptime_ms() + CHECK_MS : 0;
}

void app_pc_end(const char *why)
{
    end_session(why);
}

bool app_pc_session(void)
{
    return s_sess.active;
}

bool app_pc_keeps_wifi(void)
{
    return pc_session_keeps_wifi(&s_sess);
}

bool app_pc_pauses_cycle(void)
{
    return pc_session_pauses_cycle(&s_sess, shown_id());
}

uint8_t app_pc_saved_mode(uint8_t mode)
{
    return (uint8_t)pc_session_saved_mode(&s_sess, mode);
}

const char *app_pc_saved_preset(void)
{
    return pc_session_saved_preset(&s_sess, shown_id());
}

void app_pc_defer_night(time_t until)
{
    pc_session_defer_night(&s_sess, until);
}

/* ---- the views ---- */

const ui_pc_t *app_pc_ui(void)
{
    const settings_t *set = app_settings();
    int64_t now_ms = app_uptime_ms();
    s_view = (ui_pc_t){ .session = s_sess.active || s_demo, .catalog = s_cat_known ? &s_cat : NULL,
                        .values = s_have_vals ? &s_vals : NULL,
                        .age_ms = s_demo ? 0 : (uint32_t)(now_ms - s_heard_ms), .interval_ms = set->pc_interval_ms,
                        .since = s_since };
    snprintf(s_view.name, sizeof(s_view.name), "%s", s_info.name);
    return &s_view;
}

const ui_history_t *app_pc_history(void)
{
    return s_series != NULL ? &s_hist : NULL;
}

void app_pc_status(app_pc_status_t *out)
{
    *out = (app_pc_status_t){ .session = s_sess.active, .connected = s_connected, .demo = s_demo,
                              .hwinfo = s_hw, .readings = s_cat_known ? s_cat.count : 0, .since = s_since,
                              .age_ms = s_have_vals ? (uint32_t)(app_uptime_ms() - s_heard_ms) : 0,
                              .set_always = s_sess.set_always };
    snprintf(out->name, sizeof(out->name), "%s", s_info.name);
    snprintf(out->why, sizeof(out->why), "%s", s_why);
    snprintf(out->preset, sizeof(out->preset), "%s", s_sess.preset);
}

/* `pc demo on|off` (spec §12a.4): the sample PC for screenshots, never a session. An empty PC preset takes its
 * readings for the while, and gives them back. */
void app_pc_demo(bool on)
{
    ui_presets_t *p = app_presets();
    int i = ui_presets_find(p, "pc");
    if (on && !s_sess.active) {
        ui_pc_demo(&s_cat, &s_vals);
        s_cat_known = s_have_vals = s_demo = true;
        snprintf(s_info.name, sizeof(s_info.name), "%s", "DEMO-PC");
        s_demo_filled = ui_presets_fill_pc(p, &s_cat);
        app_pc_presets_changed();
        if (s_series != NULL) {
            ui_pc_demo_history(&s_hist, (uint32_t)app_uptime_ms());
        }
    } else if (!on && s_demo) {
        s_demo = s_have_vals = false;
        if (s_demo_filled && i >= 0) {
            for (int k = 0; k < ui_preset_slots(&p->presets[i]); k++) {
                p->presets[i].slots[k] = UI_FIELD_NONE;
                ui_presets_set_visual(p, i, k, NULL);
            }
            app_ui_save_presets();
            app_pc_presets_changed();
        }
        s_demo_filled = false;
        load_cache(); /* the PC's own catalogue again */
    }
    app_ui_render();
}

void app_pc_state_text(char *out, size_t size)
{
    const lang_t *lang = lang_get(app_settings()->language);
    if (s_demo) {
        snprintf(out, size, "%s (demo)", s_info.name);
    } else if (!app_settings()->pc_enabled) {
        snprintf(out, size, "%s", lang_str(lang, LS_PC_OFF));
    } else if (s_sess.active) {
        uint32_t age_s = s_have_vals ? (uint32_t)(app_uptime_ms() - s_heard_ms) / 1000 : 0;
        if (s_have_vals) {
            snprintf(out, size, "%s, %lu s", s_info.name, (unsigned long)age_s);
        } else {
            snprintf(out, size, "%s, %s", s_info.name, s_connected ? "\xE2\x80\xA6" : "\xE2\x80\x94");
        }
    } else if (s_since != 0) {
        ui_context_t ctx;
        app_ui_context(&ctx);
        ui_pc_since_text(&ctx, s_since, out, size);
    } else {
        const app_sync_state_t *sync = &app_state()->sync;
        bool told = sync->last_at != 0 && sync->last_detail[SYNC_STEP_PC][0] != '\0';
        snprintf(out, size, "%s", told ? sync->last_detail[SYNC_STEP_PC] : lang_str(lang, LS_PC_NONE));
    }
}
