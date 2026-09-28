#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_internal.h"
#include "diag.h"
#include "esp_console.h"
#include "esp_log.h"
#include "ui_fields.h"
#include "ui_layout.h"

/* `field` and `preset` (spec §15): inspect the dashboard and inject test data on the device. */

static const char *TAG = "app_cmds";

static int usage(const char *text)
{
    printf("usage: %s\n", text);
    return 1;
}

static void print_field(const ui_context_t *ctx, ui_field_id_t field)
{
    ui_value_t v;
    ui_resolve(ctx, field, &v);
    const char *state = v.state == UI_VALUE_FRESH ? "fresh" : v.state == UI_VALUE_STALE ? "stale" : "missing";
    printf("%-13s %-7s %s%s%s", ui_field_info(field)->id, state, v.text, v.unit[0] ? " " : "", v.unit);
    if (v.extra[0]) {
        printf(" (%s)", v.extra);
    }
    if (v.state == UI_VALUE_STALE) {
        printf(", %lu s old", (unsigned long)v.age_s);
    }
    if (v.trend) {
        printf(", %s", v.trend > 0 ? "rising" : "falling");
    }
    printf("\n");
}

/* Datastore units per console unit: 0.01 °C, 0.01 %, %, 0.1 d. */
static int scale(ds_field_t field)
{
    return field == DS_BAT_LEVEL ? 1 : field == DS_BAT_DAYS ? 10 : 100;
}

static int field_body(int argc, char **argv)
{
    static const char *const k_usage = "field list | field get <id> | field set <id> <value> | field clear <id>";
    ui_context_t ctx;
    app_ui_context(&ctx);
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int f = UI_FIELD_NONE + 1; f < UI_FIELD_COUNT; f++) {
            print_field(&ctx, (ui_field_id_t)f);
        }
        return 0;
    }
    if (argc < 3) {
        return usage(k_usage);
    }
    ui_field_id_t field = ui_field_by_name(argv[2]);
    if (field == UI_FIELD_NONE) {
        printf("field: no field \"%s\" (see `field list`)\n", argv[2]);
        return 1;
    }
    if (argc == 3 && strcmp(argv[1], "get") == 0) {
        print_field(&ctx, field);
        return 0;
    }
    int ds_field = ui_field_info(field)->ds_field;
    bool set = argc == 4 && strcmp(argv[1], "set") == 0;
    bool clear = argc == 3 && strcmp(argv[1], "clear") == 0;
    if (!set && !clear) {
        return usage(k_usage);
    }
    if (ds_field < 0) {
        printf("field: %s follows the clock and can't be set\n", argv[2]);
        return 1;
    }
    if (set) {
        char *end;
        double value = strtod(argv[3], &end);
        if (end == argv[3] || *end != '\0') {
            return usage(k_usage);
        }
        long scaled = (long)(value * scale((ds_field_t)ds_field) + (value < 0 ? -0.5 : 0.5));
        ds_set(app_ds(), (ds_field_t)ds_field, (int32_t)scaled, ctx.now);
    } else {
        ds_clear(app_ds(), (ds_field_t)ds_field);
    }
    app_ui_render();
    app_ui_context(&ctx);
    print_field(&ctx, field);
    return 0;
}

static int preset_body(int argc, char **argv)
{
    static const char *const k_usage = "preset list | preset set <id>";
    ui_presets_t *p = app_presets();
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int i = 0; i < p->count; i++) {
            const ui_preset_t *pr = &p->presets[i];
            printf("%c %-15s %-23s %-8s%s\n", i == p->active ? '*' : ' ', pr->id, pr->name,
                   ui_layout((ui_layout_id_t)pr->layout)->id, pr->in_cycle ? "" : " (not in the cycle)");
        }
        printf("auto-cycle %s, every %u s\n", p->cycle_enabled ? "on" : "off", (unsigned)p->cycle_interval_s);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "set") == 0) {
        int index = ui_presets_find(p, argv[2]);
        if (index < 0) {
            printf("preset: no preset \"%s\" (see `preset list`)\n", argv[2]);
            return 1;
        }
        app_ui_select(index, true);
        printf("preset: %s\n", p->presets[index].id);
        return 0;
    }
    return usage(k_usage);
}

static int cmd_field(int argc, char **argv)
{
    return diag_on_owner(field_body, argc, argv);
}

static int cmd_preset(int argc, char **argv)
{
    return diag_on_owner(preset_body, argc, argv);
}

void app_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "field", .help = "field list | get <id> | set <id> <value> | clear <id>", .func = &cmd_field },
        { .command = "preset", .help = "preset list | set <id>", .func = &cmd_preset },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: %s", cmds[i].command, esp_err_to_name(err));
        }
    }
}
