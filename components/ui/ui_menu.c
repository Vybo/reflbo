#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

/* The menu tree (spec §5.7). Items belong to the nearest section before them in ui_menu_item_t. */

typedef enum {
    K_SECTION,
    K_TOGGLE,
    K_CHOICE,
    K_NUMBER,
    K_DATETIME,
    K_INFO,
    K_ACTION,
} kind_t;

typedef struct {
    lang_str_t label;
    uint8_t kind;
    uint8_t parent;
    int16_t min, max, step; /* numbers */
    uint8_t decimals;
    const char *unit;
    bool confirm; /* actions */
    lang_str_t question;
} node_t;

static const node_t k_nodes[UI_MI_COUNT] = {
    [UI_MI_ROOT] = { .label = LS_MENU, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_PRESETS] = { .label = LS_M_PRESETS, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_ACTIVE_PRESET] = { .label = LS_M_ACTIVE_PRESET, .kind = K_CHOICE, .parent = UI_MI_PRESETS },
    [UI_MI_AUTO_CYCLE] = { .label = LS_M_AUTO_CYCLE, .kind = K_TOGGLE, .parent = UI_MI_PRESETS },
    [UI_MI_CYCLE_INTERVAL] = { .label = LS_M_CYCLE_INTERVAL, .kind = K_CHOICE, .parent = UI_MI_PRESETS },
    [UI_MI_SCHEDULE] = { .label = LS_M_SCHEDULE, .kind = K_TOGGLE, .parent = UI_MI_PRESETS },
    [UI_MI_WIFI] = { .label = LS_M_WIFI, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_CONFIG_MODE] = { .label = LS_M_CONFIG_MODE, .kind = K_ACTION, .parent = UI_MI_WIFI },
    [UI_MI_FORGET_NETWORKS] = { .label = LS_M_FORGET_NETWORKS, .kind = K_ACTION, .parent = UI_MI_WIFI,
                                .confirm = true, .question = LS_CONFIRM_FORGET_NETWORKS },
    [UI_MI_RESET_PASSWORD] = { .label = LS_M_RESET_PASSWORD, .kind = K_ACTION, .parent = UI_MI_WIFI,
                               .confirm = true, .question = LS_CONFIRM_RESET_PASSWORD },
    [UI_MI_SYNC] = { .label = LS_M_SYNC, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_SYNC_NOW] = { .label = LS_M_SYNC_NOW, .kind = K_ACTION, .parent = UI_MI_SYNC },
    [UI_MI_SYNC_MODE] = { .label = LS_M_SYNC_MODE, .kind = K_CHOICE, .parent = UI_MI_SYNC },
    [UI_MI_SYNC_INTERVAL] = { .label = LS_M_SYNC_INTERVAL, .kind = K_CHOICE, .parent = UI_MI_SYNC },
    [UI_MI_QUIET_HOURS] = { .label = LS_M_QUIET_HOURS, .kind = K_TOGGLE, .parent = UI_MI_SYNC },
    [UI_MI_TIME] = { .label = LS_M_TIME, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_SET_DATETIME] = { .label = LS_M_SET_DATETIME, .kind = K_DATETIME, .parent = UI_MI_TIME },
    [UI_MI_CLOCK_24H] = { .label = LS_M_CLOCK_24H, .kind = K_TOGGLE, .parent = UI_MI_TIME },
    [UI_MI_TIME_ZONE] = { .label = LS_M_TIME_ZONE, .kind = K_CHOICE, .parent = UI_MI_TIME },
    [UI_MI_DISPLAY] = { .label = LS_M_DISPLAY, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_UPDATE_INTERVAL] = { .label = LS_M_UPDATE_INTERVAL, .kind = K_NUMBER, .parent = UI_MI_DISPLAY, .min = 1,
                                .max = 15, .step = 1, .unit = "min" },
    [UI_MI_REFRESH_RATE] = { .label = LS_M_REFRESH_RATE, .kind = K_CHOICE, .parent = UI_MI_DISPLAY },
    [UI_MI_SENSORS] = { .label = LS_M_SENSORS, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_TEMP_OFFSET] = { .label = LS_M_TEMP_OFFSET, .kind = K_NUMBER, .parent = UI_MI_SENSORS, .min = -100,
                            .max = 100, .step = 1, .decimals = 1, .unit = "°C" },
    [UI_MI_HUM_OFFSET] = { .label = LS_M_HUM_OFFSET, .kind = K_NUMBER, .parent = UI_MI_SENSORS, .min = -200,
                           .max = 200, .step = 5, .decimals = 1, .unit = "%" },
    [UI_MI_UNITS] = { .label = LS_M_UNITS, .kind = K_CHOICE, .parent = UI_MI_SENSORS },
    [UI_MI_INFO] = { .label = LS_M_INFO, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_INFO_BATTERY] = { .label = LS_BATTERY, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_FIRMWARE] = { .label = LS_M_FIRMWARE, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_DEVICE] = { .label = LS_M_DEVICE, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_IP] = { .label = LS_M_IP, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_MAC] = { .label = LS_M_MAC, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_SYNC] = { .label = LS_M_LAST_SYNC, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_UPTIME] = { .label = LS_M_UPTIME, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_MEMORY] = { .label = LS_M_FREE_MEMORY, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_INFO_MQTT] = { .label = LS_M_MQTT, .kind = K_INFO, .parent = UI_MI_INFO },
    [UI_MI_SYSTEM] = { .label = LS_M_SYSTEM, .kind = K_SECTION, .parent = UI_MI_ROOT },
    [UI_MI_LANGUAGE] = { .label = LS_M_LANGUAGE, .kind = K_CHOICE, .parent = UI_MI_SYSTEM },
    [UI_MI_REBOOT] = { .label = LS_M_REBOOT, .kind = K_ACTION, .parent = UI_MI_SYSTEM },
    [UI_MI_FACTORY_RESET] = { .label = LS_M_FACTORY_RESET, .kind = K_ACTION, .parent = UI_MI_SYSTEM,
                              .confirm = true, .question = LS_CONFIRM_FACTORY_RESET },
};

static const ui_menu_intent_t k_none = { .kind = UI_MENU_NONE };

void ui_menu_open(ui_menu_t *m)
{
    memset(m, 0, sizeof(*m));
    m->section = UI_MI_ROOT;
    m->mode = UI_MENU_BROWSE;
}

int ui_menu_visible(const ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t *out, int max)
{
    int n = 0;
    for (int i = UI_MI_ROOT + 1; i < UI_MI_COUNT; i++) {
        if (k_nodes[i].parent == m->section && !model->hidden[i] && n < max) {
            out[n++] = (ui_menu_item_t)i;
        }
    }
    return n;
}

ui_menu_item_t ui_menu_current(const ui_menu_t *m, const ui_menu_model_t *model)
{
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
    return n == 0 ? UI_MI_ROOT : items[m->cursor < n ? m->cursor : n - 1];
}

const char *ui_menu_label(ui_menu_item_t item, const lang_t *lang)
{
    return (unsigned)item < UI_MI_COUNT ? lang_str(lang, k_nodes[item].label) : "";
}

bool ui_menu_is_section(ui_menu_item_t item)
{
    return (unsigned)item < UI_MI_COUNT && k_nodes[item].kind == K_SECTION;
}

const char *ui_menu_question(ui_menu_item_t item, const lang_t *lang)
{
    return (unsigned)item < UI_MI_COUNT && k_nodes[item].confirm ? lang_str(lang, k_nodes[item].question) : NULL;
}

void ui_menu_value_text(ui_menu_item_t item, int32_t value, const ui_menu_model_t *model, const lang_t *lang,
                        char *out, size_t size)
{
    out[0] = '\0';
    if ((unsigned)item >= UI_MI_COUNT) {
        return;
    }
    const node_t *n = &k_nodes[item];
    switch (n->kind) {
    case K_TOGGLE:
        snprintf(out, size, "%s", lang_str(lang, value ? LS_ON : LS_OFF));
        break;
    case K_CHOICE:
        if (model->choices[item] != NULL && value >= 0 && value < model->choice_count[item]) {
            snprintf(out, size, "%s", model->choices[item][value]);
        }
        break;
    case K_NUMBER: {
        char num[16];
        lang_format_decimal(lang, value, n->decimals, num, sizeof(num));
        bool signed_offset = n->min < 0;
        snprintf(out, size, "%s%s %s", signed_offset && value > 0 ? "+" : "", num, n->unit);
        break;
    }
    case K_INFO:
        snprintf(out, size, "%s", model->info[item] != NULL ? model->info[item] : "");
        break;
    case K_DATETIME:
        snprintf(out, size, "%02d:%02d", model->local.tm_hour, model->local.tm_min);
        break;
    default:
        break;
    }
}

static int days_in_month(int year, int month) /* month 0-11 */
{
    static const int k_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 1 && leap ? 29 : k_days[month];
}

/* Steps a date-time field by delta, wrapping within its range; the day stays valid. */
static void step_datetime(ui_menu_t *m, int delta)
{
    struct tm *t = &m->dt;
    int year = t->tm_year + 1900;
    switch (m->dt_field) {
    case 0: {
        int days = days_in_month(year, t->tm_mon);
        t->tm_mday = (t->tm_mday - 1 + delta + days) % days + 1;
        break;
    }
    case 1:
        t->tm_mon = (t->tm_mon + delta + 12) % 12;
        break;
    case 2:
        year = 2000 + (year - 2000 + delta + 100) % 100; /* the RTC holds 2000-2099 */
        t->tm_year = year - 1900;
        break;
    case 3:
        t->tm_hour = (t->tm_hour + delta + 24) % 24;
        break;
    default:
        t->tm_min = (t->tm_min + delta + 60) % 60;
        break;
    }
    int days = days_in_month(t->tm_year + 1900, t->tm_mon);
    if (t->tm_mday > days) {
        t->tm_mday = days;
    }
}

static void start_datetime(ui_menu_t *m, const struct tm *local)
{
    m->dt = (struct tm){ .tm_year = local->tm_year, .tm_mon = local->tm_mon, .tm_mday = local->tm_mday,
                         .tm_hour = local->tm_hour, .tm_min = local->tm_min, .tm_isdst = -1 };
    if (m->dt.tm_year < 126 || m->dt.tm_year > 199) {
        m->dt.tm_year = 126; /* an unset clock, or the RTC's 2000 after a power loss (D9): start from 2026 */
    }
    if (m->dt.tm_mon < 0 || m->dt.tm_mon > 11) {
        m->dt.tm_mon = 0;
    }
    int days = days_in_month(m->dt.tm_year + 1900, m->dt.tm_mon);
    if (m->dt.tm_mday < 1 || m->dt.tm_mday > days) {
        m->dt.tm_mday = 1;
    }
    m->dt_field = 0;
    m->mode = UI_MENU_DATETIME;
}

static ui_menu_intent_t select_item(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t item)
{
    const node_t *n = &k_nodes[item];
    switch (n->kind) {
    case K_SECTION:
        m->section = (uint8_t)item;
        m->cursor = 0;
        return k_none;
    case K_TOGGLE:
        return (ui_menu_intent_t){ .kind = UI_MENU_SET, .item = item, .value = !model->value[item] };
    case K_CHOICE:
        if (model->choice_count[item] == 0) {
            return k_none;
        }
        m->edit = model->value[item];
        m->mode = UI_MENU_EDIT;
        return k_none;
    case K_NUMBER:
        m->edit = model->value[item];
        m->mode = UI_MENU_EDIT;
        return k_none;
    case K_DATETIME:
        start_datetime(m, &model->local);
        return k_none;
    case K_ACTION:
        if (n->confirm) {
            m->mode = UI_MENU_CONFIRM;
            return k_none;
        }
        return (ui_menu_intent_t){ .kind = UI_MENU_ACTION, .item = item };
    default:
        return k_none; /* info: nothing to do */
    }
}

static void go_up(ui_menu_t *m, const ui_menu_model_t *model)
{
    uint8_t from = m->section;
    m->section = k_nodes[from].parent;
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
    m->cursor = 0;
    for (int i = 0; i < n; i++) {
        if (items[i] == from) {
            m->cursor = (uint8_t)i;
        }
    }
}

static void step_edit(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_item_t item, int delta)
{
    const node_t *n = &k_nodes[item];
    if (n->kind == K_CHOICE) {
        int count = model->choice_count[item];
        m->edit = count ? (m->edit + delta + count) % count : 0;
        return;
    }
    int32_t v = m->edit + delta * n->step;
    m->edit = v < n->min ? n->min : v > n->max ? n->max : v;
}

ui_menu_intent_t ui_menu_input(ui_menu_t *m, const ui_menu_model_t *model, ui_menu_key_t key)
{
    ui_menu_item_t item = ui_menu_current(m, model);
    switch (m->mode) {
    case UI_MENU_EDIT:
        if (key == UI_MENU_KEY_NEXT || key == UI_MENU_KEY_BACK) {
            step_edit(m, model, item, key == UI_MENU_KEY_NEXT ? 1 : -1);
            return k_none;
        }
        m->mode = UI_MENU_BROWSE;
        if (key == UI_MENU_KEY_SELECT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_SET, .item = item, .value = m->edit };
        }
        return k_none; /* cancelled */
    case UI_MENU_DATETIME:
        if (key == UI_MENU_KEY_NEXT || key == UI_MENU_KEY_BACK) {
            step_datetime(m, key == UI_MENU_KEY_NEXT ? 1 : -1);
            return k_none;
        }
        if (key == UI_MENU_KEY_SELECT && m->dt_field < 4) {
            m->dt_field++;
            return k_none;
        }
        m->mode = UI_MENU_BROWSE;
        if (key == UI_MENU_KEY_SELECT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_SET_TIME, .item = item, .local = m->dt };
        }
        return k_none;
    case UI_MENU_CONFIRM:
        if (key == UI_MENU_KEY_NEXT) {
            return k_none; /* only a long press confirms */
        }
        m->mode = UI_MENU_BROWSE;
        if (key == UI_MENU_KEY_SELECT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_ACTION, .item = item };
        }
        return k_none;
    default:
        break;
    }
    switch (key) {
    case UI_MENU_KEY_NEXT: {
        ui_menu_item_t items[UI_MI_COUNT];
        int n = ui_menu_visible(m, model, items, UI_MI_COUNT);
        m->cursor = n ? (uint8_t)((m->cursor + 1) % n) : 0;
        return k_none;
    }
    case UI_MENU_KEY_SELECT:
        return item == UI_MI_ROOT ? k_none : select_item(m, model, item);
    case UI_MENU_KEY_BACK:
        if (m->section == UI_MI_ROOT) {
            return (ui_menu_intent_t){ .kind = UI_MENU_CLOSE };
        }
        go_up(m, model);
        return k_none;
    default:
        return (ui_menu_intent_t){ .kind = UI_MENU_CLOSE };
    }
}
