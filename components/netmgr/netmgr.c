#include "netmgr.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "netmgr_dns.h"
#include "netmgr_link.h"
#include "nvs.h"

static const char *TAG = "netmgr";

_Static_assert(NETMGR_REASON_ASSOC_LEAVE == WIFI_REASON_ASSOC_LEAVE, "netmgr_link.h mirrors the driver");

#define TASK_STACK      6144
#define TASK_PRIORITY   4 /* below the app (5) and the buttons (6) */
#define DNS_STACK       3072
#define DNS_PRIORITY    3
#define JOIN_TIMEOUT_MS 8000 /* spec §10.1: 8 s per attempt */
#define AP_CLIENTS_MAX  2
#define LIST_VERSION    1
#define SEEN_MAX        24

typedef enum {
    CMD_START,
    CMD_STOP,
    CMD_SCAN,
    CMD_TEST,
    CMD_JOIN,   /* a sync or sync mode `always`: a saved network, never the AP (spec §9.3) */
    CMD_AP_OFF, /* config mode ended while Wi-Fi stays for a sync or `always` */
} cmd_kind_t;

typedef struct {
    uint8_t kind;
    bool keep_ap;
    int timeout_ms; /* CMD_JOIN */
    char ssid[NETMGR_SSID_MAX + 1];
    char pass[NETMGR_PASS_MAX + 1];
} cmd_t;

typedef struct {
    uint8_t version;
    netmgr_list_t list;
} stored_list_t;

#define EV_GOT_IP BIT0
#define EV_FAILED BIT1

static QueueHandle_t s_cmds;
static SemaphoreHandle_t s_lock; /* s_status and s_list: read by any task, written by the netmgr task */
static SemaphoreHandle_t s_done; /* a scan finished */
static SemaphoreHandle_t s_joined; /* a CMD_JOIN finished */
static SemaphoreHandle_t s_join_lock; /* one netmgr_join() at a time */
static esp_err_t s_join_result;
static SemaphoreHandle_t s_scan_lock; /* one netmgr_scan() at a time: the console and the web UI may both ask */
static EventGroupHandle_t s_events;
static void (*s_changed)(void);
static netmgr_status_t s_status;
static netmgr_list_t s_list;
static volatile uint8_t s_reason; /* of the last disconnection */
static volatile bool s_joining;   /* join() waits for the result itself */
static esp_netif_t *s_sta, *s_ap;
static bool s_wifi_up, s_mdns_up;
static volatile bool s_dns_run;
static TaskHandle_t s_dns_task;
static netmgr_seen_t s_seen[SEEN_MAX]; /* the last scan, on the netmgr task */
static int s_seen_n;
static netmgr_ap_t *s_scan_out;
static int s_scan_max, s_scan_n;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void notify(void)
{
    if (s_changed != NULL) {
        s_changed();
    }
}

static void set_state(netmgr_state_t state, bool ap_on)
{
    lock();
    s_status.state = state;
    s_status.ap_on = ap_on;
    if (state != NETMGR_STATION) {
        s_status.ssid[0] = '\0';
        s_status.ip[0] = '\0';
        s_status.rssi = 0;
    }
    if (!ap_on) {
        s_status.ap_clients = 0;
    }
    unlock();
    ESP_LOGI(TAG, "%s%s", state == NETMGR_OFF       ? "off"
                          : state == NETMGR_JOINING ? "joining"
                          : state == NETMGR_STATION ? "on a network"
                                                    : "AP only",
             ap_on && state != NETMGR_AP ? ", AP too" : "");
    notify();
}

static void set_test(netmgr_test_t test, const char *ssid)
{
    lock();
    s_status.test = test;
    snprintf(s_status.test_ssid, sizeof(s_status.test_ssid), "%s", ssid);
    unlock();
    notify();
}

static esp_err_t save_list(void)
{
    stored_list_t stored = { .version = LIST_VERSION };
    lock();
    stored.list = s_list;
    unlock();
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, "nets", &stored, sizeof(stored));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    memset(&stored, 0, sizeof(stored)); /* the passwords don't linger on the stack */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "saving the networks: %s", esp_err_to_name(err));
    }
    notify(); /* the app plans its syncs by the saved networks */
    return err;
}

static void load_list(void)
{
    static stored_list_t stored;
    size_t size = sizeof(stored);
    nvs_handle_t nvs;
    memset(&s_list, 0, sizeof(s_list));
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) {
        return; /* nothing saved yet */
    }
    esp_err_t err = nvs_get_blob(nvs, "nets", &stored, &size);
    nvs_close(nvs);
    if (err != ESP_OK || size != sizeof(stored) || stored.version != LIST_VERSION ||
        stored.list.count > NETMGR_LIST_MAX) {
        return;
    }
    for (int i = stored.list.count - 1; i >= 0; i--) { /* each goes first: add the last one first */
        netmgr_net_t *n = &stored.list.nets[i];
        n->ssid[NETMGR_SSID_MAX] = '\0';
        n->pass[NETMGR_PASS_MAX] = '\0';
        if (netmgr_list_add(&s_list, n->ssid, n->pass)) { /* keeps only what still reads as valid */
            memcpy(s_list.nets[0].bssid, n->bssid, 6);
            s_list.nets[0].channel = n->channel;
        }
    }
    memset(&stored, 0, sizeof(stored));
}

/* The AP password: 10 characters without look-alikes, made at the first boot and kept in NVS. */
static void load_ap_password(void)
{
    static const char k_alphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    nvs_handle_t nvs;
    size_t size = sizeof(s_status.ap_pass);
    if (nvs_open("sys", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (nvs_get_str(nvs, "ap_pass", s_status.ap_pass, &size) != ESP_OK || strlen(s_status.ap_pass) != 10) {
        for (int i = 0; i < 10; i++) {
            s_status.ap_pass[i] = k_alphabet[esp_random() % (sizeof(k_alphabet) - 1)];
        }
        s_status.ap_pass[10] = '\0';
        if (nvs_set_str(nvs, "ap_pass", s_status.ap_pass) == ESP_OK) {
            nvs_commit(nvs);
        }
    }
    nvs_close(nvs);
}

/* The station's next connect looks for any AP of its network, not only the one it joined. */
static void forget_bssid(void)
{
    static wifi_config_t c;
    if (esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK && c.sta.bssid_set) {
        c.sta.bssid_set = false;
        c.sta.channel = 0;
        esp_wifi_set_config(WIFI_IF_STA, &c);
    }
    memset(&c, 0, sizeof(c)); /* the driver keeps its own copy */
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        uint8_t reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        switch (netmgr_disconnected(reason, s_joining, s_status.state == NETMGR_STATION)) {
        case NETMGR_DISC_FAILED:
            s_reason = reason;
            xEventGroupSetBits(s_events, EV_FAILED);
            break;
        case NETMGR_DISC_RECONNECT: /* the network went away: keep trying it */
            ESP_LOGW(TAG, "lost \"%s\" (reason %u); trying again", s_status.ssid, reason);
            lock();
            s_status.ip[0] = '\0';
            unlock();
            forget_bssid(); /* a mesh node, or a new router, may carry the network now */
            esp_wifi_connect();
            notify();
            break;
        case NETMGR_DISC_IGNORE: /* the old link join() dropped, maybe late */
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        lock();
        esp_ip4addr_ntoa(&e->ip_info.ip, s_status.ip, sizeof(s_status.ip));
        unlock();
        xEventGroupSetBits(s_events, EV_GOT_IP);
        if (!s_joining) {
            notify(); /* back on the network after losing it */
        }
    } else if (base == WIFI_EVENT && (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED)) {
        wifi_sta_list_t list;
        if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
            lock();
            s_status.ap_clients = (uint8_t)list.num;
            unlock();
        }
        notify();
    }
}

/* Captive DNS (spec §10.1): every name is the AP, while the AP runs. */
static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53) };
    inet_aton(NETMGR_AP_IP, &addr.sin_addr);
    struct timeval timeout = { .tv_sec = 1 };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGE(TAG, "captive DNS: no socket");
    } else {
        static const uint8_t k_ip[4] = { 192, 168, 4, 1 };
        uint8_t query[512], reply[528];
        while (s_dns_run) {
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            int n = recvfrom(sock, query, sizeof(query), 0, (struct sockaddr *)&from, &from_len);
            if (n <= 0) {
                continue; /* the timeout: look at s_dns_run again */
            }
            size_t m = netmgr_dns_reply(query, (size_t)n, k_ip, reply, sizeof(reply));
            if (m > 0) {
                sendto(sock, reply, m, 0, (struct sockaddr *)&from, from_len);
            }
        }
    }
    if (sock >= 0) {
        close(sock);
    }
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void mdns_up(void)
{
    if (s_mdns_up || mdns_init() != ESP_OK) {
        return;
    }
    mdns_hostname_set(s_status.host);
    mdns_instance_name_set("reflbo");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    s_mdns_up = true;
}

static void ap_services(bool on)
{
    if (on && s_dns_task == NULL) {
        static const char k_uri[] = "http://" NETMGR_AP_IP "/"; /* DHCP option 114, RFC 8910 */
        esp_netif_dhcps_stop(s_ap);
        esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, (void *)k_uri, strlen(k_uri));
        esp_netif_dhcps_start(s_ap);
        s_dns_run = true;
        xTaskCreatePinnedToCore(dns_task, "dns", DNS_STACK, NULL, DNS_PRIORITY, &s_dns_task, tskNO_AFFINITY);
    } else if (!on && s_dns_task != NULL) {
        s_dns_run = false;
        for (int i = 0; i < 30 && s_dns_task != NULL; i++) { /* its receive times out within 1 s */
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* Wi-Fi on in `mode`; the AP, when it runs, on `channel`. */
static esp_err_t wifi_up(wifi_mode_t mode, uint8_t channel)
{
    if (!s_wifi_up) {
        s_sta = esp_netif_create_default_wifi_sta();
        s_ap = esp_netif_create_default_wifi_ap();
        esp_netif_set_hostname(s_sta, s_status.host);
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            return err;
        }
        esp_wifi_set_storage(WIFI_STORAGE_RAM); /* the networks live in our own NVS list */
        s_wifi_up = true;
    }
    esp_err_t err = esp_wifi_set_mode(mode);
    if (err == ESP_OK && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)) {
        wifi_config_t ap = { .ap = { .channel = channel, .max_connection = AP_CLIENTS_MAX,
                                     .authmode = WIFI_AUTH_WPA2_PSK } };
        strcpy((char *)ap.ap.ssid, s_status.ap_ssid);
        ap.ap.ssid_len = (uint8_t)strlen(s_status.ap_ssid);
        strcpy((char *)ap.ap.password, s_status.ap_pass);
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start(); /* already started: ESP_OK */
    }
    return err;
}

static void wifi_down(void)
{
    ap_services(false);
    if (s_mdns_up) {
        mdns_free();
        s_mdns_up = false;
    }
    if (s_wifi_up) {
        esp_wifi_stop();
        esp_wifi_deinit();
        esp_netif_destroy_default_wifi(s_sta);
        esp_netif_destroy_default_wifi(s_ap);
        s_sta = s_ap = NULL;
        s_wifi_up = false;
    }
}

/* A scan into s_seen. With the AP up the driver comes back to its channel between the others, so
 * its clients stay on. */
static void scan(void)
{
    static wifi_ap_record_t records[SEEN_MAX];
    uint16_t n = SEEN_MAX;
    s_seen_n = 0;
    if (esp_wifi_scan_start(NULL, true) != ESP_OK || esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) {
        return;
    }
    for (int i = 0; i < n; i++) {
        netmgr_seen_t *s = &s_seen[s_seen_n++];
        snprintf(s->ssid, sizeof(s->ssid), "%s", (const char *)records[i].ssid);
        memcpy(s->bssid, records[i].bssid, 6);
        s->channel = records[i].primary;
        s->rssi = records[i].rssi;
        s->open = records[i].authmode == WIFI_AUTH_OPEN;
    }
}

static netmgr_test_t reason_result(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return NETMGR_TEST_NOT_FOUND;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return NETMGR_TEST_WRONG_PASSWORD;
    default:
        return NETMGR_TEST_FAILED;
    }
}

/* One attempt to join; `bssid` may be NULL, which scans for the network first. */
static netmgr_test_t join(const char *ssid, const char *pass, const uint8_t *bssid, uint8_t channel)
{
    static wifi_config_t c;
    memset(&c, 0, sizeof(c));
    strcpy((char *)c.sta.ssid, ssid);
    strcpy((char *)c.sta.password, pass);
    c.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    if (bssid != NULL) {
        c.sta.bssid_set = true;
        memcpy(c.sta.bssid, bssid, 6);
        c.sta.channel = channel;
    }
    s_joining = true;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_events, EV_GOT_IP | EV_FAILED);
    s_reason = 0;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &c);
    memset(&c, 0, sizeof(c)); /* the driver keeps its own copy */
    if (err != ESP_OK || esp_wifi_connect() != ESP_OK) {
        s_joining = false;
        return NETMGR_TEST_FAILED;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, EV_GOT_IP | EV_FAILED, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(JOIN_TIMEOUT_MS));
    netmgr_test_t r = bits & EV_GOT_IP   ? NETMGR_TEST_OK
                      : bits & EV_FAILED ? reason_result(s_reason)
                                         : NETMGR_TEST_FAILED;
    if (r != NETMGR_TEST_OK) {
        esp_wifi_disconnect();
    }
    s_joining = false;
    return r;
}

/* Joined saved network `ssid`: remember where, and show it. The list may have lost it meanwhile (a
 * forget from the web UI during the join), which leaves the list alone. */
static void joined(const char *ssid, bool ap_on)
{
    wifi_ap_record_t info;
    uint8_t bssid[6] = { 0 }, channel = 0;
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        memcpy(bssid, info.bssid, 6);
        channel = info.primary;
        rssi = info.rssi;
    }
    static netmgr_list_t before;
    lock();
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", ssid);
    s_status.rssi = rssi;
    before = s_list;
    netmgr_list_succeeded(&s_list, netmgr_list_find(&s_list, ssid), bssid, channel);
    bool changed = memcmp(&before, &s_list, sizeof(before)) != 0;
    unlock();
    memset(&before, 0, sizeof(before));
    if (changed) { /* spec §10.1: frequent syncs on the same network don't wear the flash */
        save_list();
    }
    mdns_up();
    ESP_LOGI(TAG, "joined \"%s\" as %s", s_status.ssid, s_status.ip);
    set_state(NETMGR_STATION, ap_on);
}

/* Tries the saved networks in order, each first where it was last found; true once one joined. */
static int64_t s_join_until_us; /* a sync's join starts no attempt that would end after this; 0: none */

static bool join_time_left(void)
{
    return s_join_until_us == 0 || esp_timer_get_time() + (int64_t)JOIN_TIMEOUT_MS * 1000 <= s_join_until_us;
}

static bool join_saved(bool ap_on)
{
    static netmgr_list_t list;
    lock();
    list = s_list;
    unlock();
    bool ok = false;
    for (int i = 0; i < list.count && !ok && (i == 0 || join_time_left()); i++) {
        const netmgr_net_t *n = &list.nets[i];
        bool cached = n->channel != 0;
        netmgr_test_t r = join(n->ssid, n->pass, cached ? n->bssid : NULL, n->channel);
        if (r != NETMGR_TEST_OK && cached && join_time_left()) {
            r = join(n->ssid, n->pass, NULL, 0); /* the router may have moved */
        }
        if (r == NETMGR_TEST_OK) {
            joined(n->ssid, ap_on);
            ok = true;
        } else {
            ESP_LOGI(TAG, "\"%s\": %s", n->ssid, netmgr_test_name(r));
        }
    }
    memset(&list, 0, sizeof(list));
    return ok;
}

/* No network: the AP and its captive portal (spec §10.2), on the channel of the network the owner
 * will likely pick. The station stays on for scans and tests. */
static void start_ap(void)
{
    lock();
    netmgr_list_t saved = s_list;
    unlock();
    if (wifi_up(WIFI_MODE_STA, 1) == ESP_OK) {
        scan();
    }
    uint8_t channel = netmgr_ap_channel(s_seen, s_seen_n, &saved);
    memset(&saved, 0, sizeof(saved));
    if (wifi_up(WIFI_MODE_APSTA, channel) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi didn't start");
        set_state(NETMGR_OFF, false);
        return;
    }
    ap_services(true);
    mdns_up();
    ESP_LOGI(TAG, "AP on channel %u", channel);
    set_state(NETMGR_AP, true);
}

/* The AP beside the station that already runs (a sync's or sync mode `always`'s), on its channel. */
static void add_ap(void)
{
    uint8_t channel = 1;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&channel, &second);
    if (wifi_up(WIFI_MODE_APSTA, channel) == ESP_OK) {
        ap_services(true);
    }
}

static void do_start(bool keep_ap)
{
    lock();
    int saved = s_list.count;
    netmgr_state_t state = s_status.state;
    bool ap_on = s_status.ap_on;
    bool on_ip = s_status.ip[0] != '\0'; /* without one, `always` is rejoining a lost network */
    unlock();
    if (state == NETMGR_STATION && on_ip) { /* config mode joins the network a sync or `always` is on */
        if (keep_ap && !ap_on) {
            add_ap();
        }
        set_state(NETMGR_STATION, keep_ap || ap_on);
        return;
    }
    if (saved > 0) {
        set_state(NETMGR_JOINING, keep_ap);
        if (wifi_up(keep_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA, 1) != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi didn't start");
            set_state(NETMGR_OFF, false);
            return;
        }
        ap_services(keep_ap);
        if (join_saved(keep_ap)) {
            return;
        }
    }
    start_ap();
}

static void do_scan(void)
{
    s_scan_n = 0;
    wifi_mode_t mode;
    if (s_wifi_up && esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)) {
        scan();
        s_scan_n = netmgr_seen_unique(s_seen, s_seen_n, s_scan_out, s_scan_max);
    }
}

static netmgr_test_t do_test(const char *ssid, const char *pass)
{
    netmgr_status_t before;
    netmgr_status(&before);
    wifi_mode_t mode;
    if (!s_wifi_up || esp_wifi_get_mode(&mode) != ESP_OK) {
        return NETMGR_TEST_FAILED;
    }
    if (mode == WIFI_MODE_STA) { /* spec §10.2: AP and station together while testing */
        uint8_t channel = 1;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&channel, &second);
        if (wifi_up(WIFI_MODE_APSTA, channel) != ESP_OK) {
            return NETMGR_TEST_FAILED;
        }
        ap_services(true);
    } /* a running AP is left alone: setting its config again would drop the phone on it */
    scan();
    int best = netmgr_seen_best(s_seen, s_seen_n, ssid);
    netmgr_test_t r = NETMGR_TEST_NOT_FOUND; /* out of reach: the AP never leaves its channel */
    if (best >= 0) {
        r = join(ssid, pass, s_seen[best].bssid, s_seen[best].channel);
    }
    if (r == NETMGR_TEST_OK) {
        lock();
        netmgr_list_add(&s_list, ssid, pass);
        unlock();
        joined(ssid, true);
        return r;
    }
    ESP_LOGI(TAG, "test \"%s\": %s", ssid, netmgr_test_name(r));
    if (before.state == NETMGR_STATION && !join_saved(true)) { /* back where it was, if it can */
        set_state(NETMGR_AP, true);
    }
    return r;
}

/* A saved network for a sync, without the AP: ESP_OK on it, ESP_ERR_NOT_FOUND with none saved,
 * ESP_FAIL if none joined within `timeout_ms`, ESP_ERR_INVALID_STATE while the AP runs without a
 * network (config mode). A station without an address is rejoining a lost network: joined again. */
static esp_err_t do_join(int timeout_ms)
{
    lock();
    int saved = s_list.count;
    netmgr_state_t state = s_status.state;
    bool ap_on = s_status.ap_on;
    bool on_ip = s_status.ip[0] != '\0';
    unlock();
    if (state == NETMGR_STATION && on_ip) {
        return ESP_OK;
    }
    if (state == NETMGR_AP || state == NETMGR_JOINING || (state == NETMGR_STATION && ap_on)) {
        return ESP_ERR_INVALID_STATE; /* config mode's AP stays: it rejoins by itself, or the phone helps */
    }
    if (saved == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    set_state(NETMGR_JOINING, false);
    s_join_until_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    bool ok = wifi_up(WIFI_MODE_STA, 1) == ESP_OK && join_saved(false);
    s_join_until_us = 0;
    if (ok) {
        return ESP_OK;
    }
    wifi_down();
    set_state(NETMGR_OFF, false);
    return ESP_FAIL;
}

static void do_ap_off(void)
{
    lock();
    netmgr_state_t state = s_status.state;
    unlock();
    if (state == NETMGR_STATION) {
        ap_services(false);
        esp_wifi_set_mode(WIFI_MODE_STA);
        set_state(NETMGR_STATION, false);
    } else {
        wifi_down();
        set_test(NETMGR_TEST_NONE, "");
        set_state(NETMGR_OFF, false);
    }
}

static void netmgr_task(void *arg)
{
    (void)arg;
    for (;;) {
        static cmd_t cmd;
        xQueueReceive(s_cmds, &cmd, portMAX_DELAY);
        switch (cmd.kind) {
        case CMD_START:
            do_start(cmd.keep_ap);
            break;
        case CMD_STOP:
            wifi_down();
            set_test(NETMGR_TEST_NONE, "");
            set_state(NETMGR_OFF, false);
            break;
        case CMD_SCAN:
            do_scan();
            xSemaphoreGive(s_done);
            break;
        case CMD_TEST:
            set_test(do_test(cmd.ssid, cmd.pass), cmd.ssid);
            break;
        case CMD_JOIN:
            s_join_result = do_join(cmd.timeout_ms);
            xSemaphoreGive(s_joined);
            break;
        case CMD_AP_OFF:
            do_ap_off();
            break;
        }
        memset(&cmd, 0, sizeof(cmd)); /* a test's password doesn't linger */
    }
}

esp_err_t netmgr_init(void (*changed)(void))
{
    if (s_cmds != NULL) {
        return ESP_ERR_INVALID_STATE; /* once per boot */
    }
    s_changed = changed;
    s_lock = xSemaphoreCreateMutex();
    s_done = xSemaphoreCreateBinary();
    s_scan_lock = xSemaphoreCreateMutex();
    s_joined = xSemaphoreCreateBinary();
    s_join_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    QueueHandle_t cmds = xQueueCreate(4, sizeof(cmd_t));
    if (s_lock == NULL || s_done == NULL || s_scan_lock == NULL || s_joined == NULL || s_join_lock == NULL ||
        s_events == NULL || cmds == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_cmds = cmds;
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_status.host, sizeof(s_status.host), "reflbo-%02x%02x", mac[4], mac[5]);
    snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "%s", s_status.host);
    load_ap_password();
    load_list();
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        err = err == ESP_ERR_INVALID_STATE ? ESP_OK : err; /* someone made it already */
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    }
    if (err == ESP_OK && xTaskCreatePinnedToCore(netmgr_task, "netmgr", TASK_STACK, NULL, TASK_PRIORITY, NULL,
                                                 tskNO_AFFINITY) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "%d saved network(s), AP %s", s_list.count, s_status.ap_ssid);
    return err;
}

static void post(const cmd_t *cmd)
{
    xQueueSend(s_cmds, cmd, portMAX_DELAY);
}

void netmgr_start(bool keep_ap)
{
    post(&(cmd_t){ .kind = CMD_START, .keep_ap = keep_ap });
}

void netmgr_stop(void)
{
    post(&(cmd_t){ .kind = CMD_STOP });
}

esp_err_t netmgr_join(int timeout_ms)
{
    xSemaphoreTake(s_join_lock, portMAX_DELAY);
    post(&(cmd_t){ .kind = CMD_JOIN, .timeout_ms = timeout_ms });
    xSemaphoreTake(s_joined, portMAX_DELAY); /* join_saved() ends its last attempt by the limit */
    esp_err_t err = s_join_result;
    xSemaphoreGive(s_join_lock);
    return err;
}

void netmgr_ap_off(void)
{
    post(&(cmd_t){ .kind = CMD_AP_OFF });
}

void netmgr_status(netmgr_status_t *out)
{
    lock();
    *out = s_status;
    unlock();
}

int netmgr_scan(netmgr_ap_t *out, int max)
{
    xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    s_scan_out = out;
    s_scan_max = max;
    post(&(cmd_t){ .kind = CMD_SCAN });
    xSemaphoreTake(s_done, portMAX_DELAY);
    int n = s_scan_n;
    xSemaphoreGive(s_scan_lock);
    return n;
}

esp_err_t netmgr_test_start(const char *ssid, const char *pass)
{
    if (!netmgr_net_valid(ssid, pass)) {
        return ESP_ERR_INVALID_ARG;
    }
    lock();
    bool busy = s_status.test == NETMGR_TEST_RUNNING || s_status.state == NETMGR_OFF;
    if (!busy) {
        s_status.test = NETMGR_TEST_RUNNING;
        snprintf(s_status.test_ssid, sizeof(s_status.test_ssid), "%s", ssid);
    }
    unlock();
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    static cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = CMD_TEST;
    snprintf(cmd.ssid, sizeof(cmd.ssid), "%s", ssid);
    snprintf(cmd.pass, sizeof(cmd.pass), "%s", pass);
    post(&cmd);
    memset(&cmd, 0, sizeof(cmd));
    return ESP_OK;
}

void netmgr_networks(netmgr_list_t *out)
{
    lock();
    *out = s_list;
    unlock();
    for (int i = 0; i < out->count; i++) {
        memset(out->nets[i].pass, 0, sizeof(out->nets[i].pass));
    }
}

esp_err_t netmgr_add(const char *ssid, const char *pass)
{
    lock();
    bool ok = netmgr_list_add(&s_list, ssid, pass);
    unlock();
    return ok ? save_list() : ESP_ERR_INVALID_ARG;
}

esp_err_t netmgr_forget(const char *ssid)
{
    lock();
    bool found = netmgr_list_forget(&s_list, ssid);
    unlock();
    return found ? save_list() : ESP_ERR_NOT_FOUND;
}

esp_err_t netmgr_forget_all(void)
{
    lock();
    memset(&s_list, 0, sizeof(s_list));
    unlock();
    return save_list();
}

const char *netmgr_test_name(netmgr_test_t result)
{
    static const char *const k_names[] = { "none",      "testing", "connected", "wrong password",
                                           "not found", "failed",  "invalid" };
    return (unsigned)result < sizeof(k_names) / sizeof(k_names[0]) ? k_names[result] : "?";
}
