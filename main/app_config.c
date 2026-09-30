#include <stdint.h>
#include <stdio.h>

#include "app.h"
#include "app_internal.h"
#include "board_buttons.h"
#include "display.h"
#include "esp_log.h"
#include "lang.h"
#include "netmgr.h"
#include "st7305.h"
#include "ui_screens.h"
#include "webui.h"

/* Config mode (spec §10.2): Wi-Fi, the web configurator and their screen. It belongs to the app
 * task; netmgr and webui report back through app_post(). */

#define IDLE_MS (10 * 60 * 1000) /* spec §10.2: 10 min without HTTP requests */

static const char *TAG = "app_config";

/* Config mode binds KEY short (the other QR code) and BOOT long (exit), both at once (spec §5.6). */
static const gesture_config_t k_config_buttons[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY] = { .long_ms = 1000, .double_enabled = false },
    [BOARD_BUTTON_BOOT] = { .long_ms = 1000, .double_enabled = false },
};

static bool s_net_ready, s_on, s_qr_url;
static bool s_setup_asked; /* KEY short brought the setup screen back while a phone is logged in */
static int64_t s_started_ms;
static int s_shown_minutes;

static void net_changed_on_app(void *arg)
{
    (void)arg;
    if (s_on) {
        app_ui_render();
    }
}

static void net_changed(void) /* on the netmgr task or the event loop */
{
    app_post(net_changed_on_app, NULL);
}

esp_err_t app_net_init(void)
{
    if (s_net_ready) {
        return ESP_OK;
    }
    esp_err_t err = netmgr_init(net_changed);
    s_net_ready = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager: %s", esp_err_to_name(err));
    }
    return err;
}

static void web_event_on_app(void *arg)
{
    webui_event_t event = (webui_event_t)(intptr_t)arg;
    switch (event) {
    case WEBUI_EVENT_DONE:
        app_config_exit();
        break;
    case WEBUI_EVENT_REBOOT:
        app_restart(LS_T_REBOOTING, true);
        break;
    case WEBUI_EVENT_UPDATED:
        app_restart(LS_T_UPDATED, true);
        break;
    case WEBUI_EVENT_FACTORY_RESET:
        app_factory_reset();
        break;
    case WEBUI_EVENT_SESSION: /* D20: the dashboard while someone is logged in, the setup screen after */
        s_setup_asked = false;
        if (s_on) {
            app_ui_render();
        }
        break;
    }
}

static void web_event(webui_event_t event) /* on the server's task, after the reply went out */
{
    app_post(web_event_on_app, (void *)(intptr_t)event);
}

void app_config_enter(void)
{
    if (s_on || app_state()->critical || app_net_init() != ESP_OK) {
        return;
    }
    app_menu_close();
    app_ui_end_first_run();
    s_on = true;
    s_qr_url = false;
    s_setup_asked = false;
    s_started_ms = app_uptime_ms();
    board_buttons_set_config(k_config_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_HPM); /* spec §9.1: the screen answers at once */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HPM: %s", esp_err_to_name(err));
    }
    bool no_password = !webui_password_set();
    netmgr_start(no_password); /* D18: only a phone on the AP may choose the password */
    const webui_config_t web = { .run = app_execute, .api = app_web_api, .event = web_event };
    err = webui_start(&web);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web configurator: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "config mode on%s", no_password ? ", no web password yet" : "");
    app_ui_render();
}

void app_config_exit(void)
{
    if (!s_on) {
        return;
    }
    s_on = false;
    webui_stop();
    netmgr_stop();
    board_buttons_set_config(k_app_dashboard_buttons);
    esp_err_t err = st7305_set_mode(ST7305_MODE_LPM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LPM: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "config mode off");
    app_ui_toast(lang_str(lang_get(app_settings()->language), LS_T_WIFI_OFF));
}

bool app_config_active(void)
{
    return s_on;
}

bool app_config_shows_setup(void)
{
    return s_on && (s_setup_asked || !webui_session_active());
}

void app_config_key(void)
{
    if (webui_session_active()) {
        s_setup_asked = !s_setup_asked; /* for another phone to join; KEY again goes back */
    } else {
        s_qr_url = !s_qr_url;
    }
    app_ui_render();
}

/* Config mode ends 10 min after it began or after the last request, whichever is later. */
int64_t app_config_deadline_ms(void)
{
    if (!s_on) {
        return 0;
    }
    int64_t last = webui_last_request_ms();
    return (last > s_started_ms ? last : s_started_ms) + IDLE_MS;
}

static int minutes_left(void)
{
    int64_t left = app_config_deadline_ms() - app_uptime_ms();
    return left <= 0 ? 0 : (int)((left + 59999) / 60000);
}

int64_t app_config_redraw_ms(void)
{
    if (!app_config_shows_setup()) {
        return 0; /* the dashboard keeps its own schedule */
    }
    int m = minutes_left(); /* the count drops at the next whole minute before the deadline */
    return m > 0 ? app_config_deadline_ms() - (int64_t)(m - 1) * 60000 : app_config_deadline_ms();
}

void app_config_tick(void)
{
    if (!s_on) {
        return;
    }
    /* No battery check: config mode takes no battery samples, as the radio's load pulls VBAT down
     * (spec §8, §10.2); the idle timeout bounds it. */
    if (app_uptime_ms() >= app_config_deadline_ms()) {
        ESP_LOGI(TAG, "no requests for %d min", IDLE_MS / 60000);
        app_config_exit();
    } else if (app_config_shows_setup() && minutes_left() != s_shown_minutes) {
        app_ui_render();
    }
}

static ui_net_state_t view_state(netmgr_state_t state)
{
    switch (state) {
    case NETMGR_JOINING:
        return UI_NET_JOINING;
    case NETMGR_STATION:
        return UI_NET_STATION;
    case NETMGR_AP:
        return UI_NET_AP;
    default:
        return UI_NET_STARTING; /* the start command is still on its way */
    }
}

void app_config_draw(gfx_fb_t *fb, const lang_t *lang)
{
    static netmgr_status_t st;
    netmgr_status(&st);
    s_shown_minutes = minutes_left();
    ui_config_view_t v = { .state = view_state(st.state), .ap_on = st.ap_on, .qr_url = s_qr_url, .ssid = st.ssid,
                           .ip = st.ip, .host = st.host, .ap_ssid = st.ap_ssid, .ap_pass = st.ap_pass,
                           .minutes_left = s_shown_minutes };
    ui_draw_config(fb, &v, lang);
}
