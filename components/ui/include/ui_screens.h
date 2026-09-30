#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"
#include "ui_fields.h"

/* Special screens (spec §5.5). Pure C, host-buildable. */

/* A short message over whatever is on screen, for about 3 s: "Preset: Indoor". */
void ui_draw_toast(gfx_fb_t *fb, const char *text);
/* The last screen before the battery gives out (spec §8): nothing else updates after it. */
void ui_draw_critical(gfx_fb_t *fb, const ui_context_t *ctx);
/* The first run (spec §5.5): what the buttons do, and the clock if the time is valid. */
void ui_draw_first_run(gfx_fb_t *fb, const ui_context_t *ctx);

/* Config mode (spec §5.5, §10.2), as the app sees the Wi-Fi manager. */
typedef enum {
    UI_NET_STARTING, /* Wi-Fi is on its way up */
    UI_NET_JOINING,  /* trying the saved networks */
    UI_NET_STATION,  /* on a network */
    UI_NET_AP,       /* only the device's own AP */
} ui_net_state_t;

typedef struct {
    ui_net_state_t state;
    bool ap_on;          /* the AP runs: the AP state, or beside a station while no web password is set */
    bool qr_url;         /* KEY short picked the code that opens the web UI; otherwise it joins the AP */
    bool back;           /* a phone is logged in and KEY brought this up: KEY goes back to the dashboard (D20) */
    const char *ssid;    /* the network joined, or being joined */
    const char *ip;      /* on that network; "" while it has none */
    const char *host;    /* reflbo-XXXX, the mDNS name without .local */
    const char *ap_ssid;
    const char *ap_pass;
    int minutes_left; /* before config mode ends by itself */
} ui_config_view_t;

typedef enum {
    UI_QR_NONE,
    UI_QR_JOIN, /* joins the device's AP: "WIFI:T:WPA;S:...;P:...;;" */
    UI_QR_OPEN, /* opens the web UI: "http://<ip>/" */
} ui_qr_kind_t;

/* The QR code the screen shows: the one `qr_url` asks for if it exists now, else the other. */
ui_qr_kind_t ui_config_qr(const ui_config_view_t *v, char *out, size_t size);
/* Both codes exist, so KEY short has something to switch to. */
bool ui_config_can_switch(const ui_config_view_t *v);
void ui_draw_config(gfx_fb_t *fb, const ui_config_view_t *v, const lang_t *lang);
