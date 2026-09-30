#include "webui.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "netmgr.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "webui_auth.h"
#include "webui_http.h"

static const char *TAG = "webui";

#define SERVER_STACK    8192
#define SERVER_PRIORITY 4 /* below the app task (5): API calls wait for it anyway */
#define STOPPER_STACK   3072
#define OTA_CHUNK       4096
#define COOKIE_ATTRS    "; Path=/; HttpOnly; SameSite=Strict"

/* The pages, gzipped when the firmware is built (CMakeLists.txt). */
#define WEB_ASSET(name)                                              \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end")
WEB_ASSET(index_html_gz);
WEB_ASSET(app_js_gz);
WEB_ASSET(style_css_gz);
WEB_ASSET(zones_js_gz);

typedef struct {
    const char *uri;
    const uint8_t *start, *end;
    const char *type;
} asset_t;

static const asset_t k_assets[] = {
    { "/", index_html_gz_start, index_html_gz_end, "text/html; charset=utf-8" },
    { "/app.js", app_js_gz_start, app_js_gz_end, "text/javascript; charset=utf-8" },
    { "/style.css", style_css_gz_start, style_css_gz_end, "text/css; charset=utf-8" },
    { "/zones.js", zones_js_gz_start, zones_js_gz_end, "text/javascript; charset=utf-8" },
};

static httpd_handle_t s_server;
static volatile bool s_stopping;
static webui_config_t s_cfg;
static char *s_body;   /* PSRAM, one request at a time: the server has one task */
static uint8_t *s_out; /* PSRAM */
static SemaphoreHandle_t s_auth_lock; /* the sessions and the stored record; the menu may reset them */
static webui_sessions_t s_sessions;
static volatile int64_t s_last_ms;

static int64_t now_s(void)
{
    return esp_timer_get_time() / 1000000;
}

static void touch(void)
{
    s_last_ms = esp_timer_get_time() / 1000;
}

/* The password's record in NVS `secrets` (D18), "" when none is set. */
static bool record_get(char *out, size_t size)
{
    nvs_handle_t nvs;
    out[0] = '\0';
    if (nvs_open("secrets", NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_str(nvs, "web_pass", out, &size);
    nvs_close(nvs);
    if (err != ESP_OK) {
        out[0] = '\0';
    }
    return out[0] != '\0';
}

static esp_err_t record_put(const char *record)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("secrets", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = record[0] ? nvs_set_str(nvs, "web_pass", record) : nvs_erase_key(nvs, "web_pass");
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; /* nothing to erase */
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

bool webui_password_set(void)
{
    char record[WEBUI_AUTH_RECORD_LEN];
    return record_get(record, sizeof(record));
}

esp_err_t webui_reset_password(void)
{
    if (s_auth_lock == NULL) {
        s_auth_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_auth_lock, portMAX_DELAY);
    webui_sessions_clear(&s_sessions);
    esp_err_t err = record_put("");
    xSemaphoreGive(s_auth_lock);
    ESP_LOGI(TAG, "web password reset");
    return err;
}

/* The client is on the device's own AP: 192.168.4.0/24, maybe as an IPv4-mapped IPv6 address. */
static bool peer_on_ap(httpd_req_t *req)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&addr, &len) != 0) {
        return false;
    }
    uint32_t ip = 0;
    if (addr.ss_family == AF_INET) {
        ip = ntohl(((struct sockaddr_in *)&addr)->sin_addr.s_addr);
    } else if (addr.ss_family == AF_INET6) {
        const uint8_t *b = ((struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr;
        static const uint8_t k_mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
        if (memcmp(b, k_mapped, sizeof(k_mapped)) != 0) {
            return false;
        }
        ip = (uint32_t)b[12] << 24 | (uint32_t)b[13] << 16 | (uint32_t)b[14] << 8 | b[15];
    }
    return (ip & 0xFFFFFF00u) == 0xC0A80400u;
}

/* The request names this device; a captive-portal probe names someone else's host. */
static bool for_us(httpd_req_t *req)
{
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return true; /* HTTP/1.0 */
    }
    netmgr_status_t st;
    netmgr_status(&st);
    char local[sizeof(st.host) + 8];
    snprintf(local, sizeof(local), "%s.local", st.host);
    return webui_host_is(host, NETMGR_AP_IP) || webui_host_is(host, st.host) || webui_host_is(host, local) ||
           (st.ip[0] != '\0' && webui_host_is(host, st.ip));
}

static esp_err_t to_portal(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" NETMGR_AP_IP "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "reflbo setup"); /* iOS wants a body to show the portal */
}

static const char *status_line(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Content Too Large";
    case 415: return "415 Unsupported Media Type";
    case 429: return "429 Too Many Requests";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
    }
}

static esp_err_t send_json(httpd_req_t *req, int status, const char *json)
{
    httpd_resp_set_status(req, status_line(status));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", message);
    char *text = cJSON_PrintUnformatted(o);
    esp_err_t err = send_json(req, status, text ? text : "{}");
    cJSON_free(text);
    cJSON_Delete(o);
    return err;
}

static esp_err_t send_cjson(httpd_req_t *req, int status, cJSON *o)
{
    char *text = cJSON_PrintUnformatted(o);
    esp_err_t err = send_json(req, status, text ? text : "{}");
    cJSON_free(text);
    cJSON_Delete(o);
    return err;
}

/* The whole body into s_body, NUL-terminated. */
static bool read_body(httpd_req_t *req)
{
    if (req->content_len > WEBUI_BODY_MAX) {
        return false;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, s_body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        got += (size_t)n;
    }
    s_body[got] = '\0';
    return true;
}

static bool session_ok(httpd_req_t *req)
{
    char cookie[160], token[WEBUI_TOKEN_LEN + 1];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK ||
        !webui_cookie_token(cookie, token)) {
        return false;
    }
    xSemaphoreTake(s_auth_lock, portMAX_DELAY);
    bool ok = webui_session_check(&s_sessions, token, now_s());
    xSemaphoreGive(s_auth_lock);
    return ok;
}

static void set_session_cookie(httpd_req_t *req)
{
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    static char header[8 + WEBUI_TOKEN_LEN + sizeof(COOKIE_ATTRS)];
    xSemaphoreTake(s_auth_lock, portMAX_DELAY);
    snprintf(header, sizeof(header), "session=%s" COOKIE_ATTRS, webui_session_new(&s_sessions, random, now_s()));
    xSemaphoreGive(s_auth_lock);
    httpd_resp_set_hdr(req, "Set-Cookie", header); /* the header is sent before this returns */
}

static const char *json_string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static esp_err_t auth_routes(httpd_req_t *req, const char *path)
{
    char record[WEBUI_AUTH_RECORD_LEN];
    bool set = record_get(record, sizeof(record));
    if (strcmp(path, "/api/auth") == 0 && req->method == HTTP_GET) {
        netmgr_status_t st;
        netmgr_status(&st);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "password_set", set);
        cJSON_AddBoolToObject(o, "logged_in", set && session_ok(req));
        cJSON_AddBoolToObject(o, "on_ap", peer_on_ap(req));
        cJSON_AddStringToObject(o, "host", st.host);
        cJSON_AddStringToObject(o, "ap_ssid", st.ap_ssid);
        cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
        return send_cjson(req, 200, o);
    }
    if (req->method != HTTP_POST) {
        return send_error(req, 405, "use POST");
    }
    if (!read_body(req)) {
        return send_error(req, 413, "request too large");
    }
    cJSON *in = cJSON_Parse(s_body);
    const char *password = json_string(in, "password");
    esp_err_t err;
    if (strcmp(path, "/api/auth/setup") == 0) {
        if (set) {
            err = send_error(req, 409, "a password is set already");
        } else if (!peer_on_ap(req)) {
            err = send_error(req, 403, "join the device's own Wi-Fi to choose the password");
        } else if (!webui_password_valid(password)) {
            err = send_error(req, 400, "the password needs 8-64 characters");
        } else {
            uint8_t salt[WEBUI_AUTH_SALT_LEN];
            esp_fill_random(salt, sizeof(salt));
            if (!webui_auth_record(password, salt, WEBUI_AUTH_ITERATIONS, record, sizeof(record)) ||
                record_put(record) != ESP_OK) {
                err = send_error(req, 500, "the password couldn't be saved");
            } else {
                ESP_LOGI(TAG, "web password set");
                set_session_cookie(req);
                err = send_json(req, 200, "{\"ok\":true}");
            }
        }
    } else if (strcmp(path, "/api/auth/login") == 0) {
        xSemaphoreTake(s_auth_lock, portMAX_DELAY);
        bool allowed = webui_login_allowed(&s_sessions, now_s());
        xSemaphoreGive(s_auth_lock);
        if (!set) {
            err = send_error(req, 409, "choose a password first");
        } else if (!allowed) {
            err = send_error(req, 429, "too many tries; wait a minute");
        } else {
            bool ok = password != NULL && webui_auth_check(record, password);
            xSemaphoreTake(s_auth_lock, portMAX_DELAY);
            webui_login_result(&s_sessions, ok, now_s());
            xSemaphoreGive(s_auth_lock);
            if (ok) {
                set_session_cookie(req);
                err = send_json(req, 200, "{\"ok\":true}");
            } else {
                ESP_LOGW(TAG, "a failed login");
                err = send_error(req, 401, "wrong password");
            }
        }
    } else if (strcmp(path, "/api/auth/logout") == 0) {
        char cookie[160], token[WEBUI_TOKEN_LEN + 1];
        if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) == ESP_OK &&
            webui_cookie_token(cookie, token)) {
            xSemaphoreTake(s_auth_lock, portMAX_DELAY);
            for (int i = 0; i < WEBUI_SESSIONS; i++) {
                if (strcmp(s_sessions.s[i].token, token) == 0) {
                    s_sessions.s[i].token[0] = '\0';
                }
            }
            xSemaphoreGive(s_auth_lock);
        }
        httpd_resp_set_hdr(req, "Set-Cookie", "session=; Max-Age=0" COOKIE_ATTRS);
        err = send_json(req, 200, "{\"ok\":true}");
    } else if (strcmp(path, "/api/auth/password") == 0) {
        const char *old = json_string(in, "old");
        if (!set || !session_ok(req)) {
            err = send_error(req, 401, "log in first");
        } else if (old == NULL || !webui_auth_check(record, old)) {
            err = send_error(req, 403, "the current password is wrong");
        } else if (!webui_password_valid(password)) {
            err = send_error(req, 400, "the password needs 8-64 characters");
        } else {
            uint8_t salt[WEBUI_AUTH_SALT_LEN];
            esp_fill_random(salt, sizeof(salt));
            err = webui_auth_record(password, salt, WEBUI_AUTH_ITERATIONS, record, sizeof(record)) &&
                          record_put(record) == ESP_OK
                      ? send_json(req, 200, "{\"ok\":true}")
                      : send_error(req, 500, "the password couldn't be saved");
        }
    } else {
        err = send_error(req, 404, "no such API");
    }
    memset(s_body, 0, req->content_len); /* passwords don't linger */
    cJSON_Delete(in);
    return err;
}

static esp_err_t wifi_routes(httpd_req_t *req, const char *path, const char *query)
{
    if (strcmp(path, "/api/wifi/scan") == 0 && req->method == HTTP_GET) {
        static netmgr_ap_t aps[NETMGR_SCAN_MAX];
        int n = netmgr_scan(aps, NETMGR_SCAN_MAX);
        cJSON *o = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(o, "networks");
        for (int i = 0; i < n; i++) {
            cJSON *ap = cJSON_CreateObject();
            cJSON_AddStringToObject(ap, "ssid", aps[i].ssid);
            cJSON_AddNumberToObject(ap, "rssi", aps[i].rssi);
            cJSON_AddBoolToObject(ap, "open", aps[i].open);
            cJSON_AddItemToArray(list, ap);
        }
        return send_cjson(req, 200, o);
    }
    if (strcmp(path, "/api/wifi/networks") != 0) {
        return send_error(req, 404, "no such API");
    }
    if (req->method == HTTP_GET) {
        netmgr_list_t saved;
        netmgr_networks(&saved);
        netmgr_status_t st;
        netmgr_status(&st);
        cJSON *o = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(o, "saved");
        for (int i = 0; i < saved.count; i++) {
            cJSON_AddItemToArray(list, cJSON_CreateString(saved.nets[i].ssid));
        }
        cJSON_AddStringToObject(o, "state", st.state == NETMGR_STATION ? "station"
                                            : st.state == NETMGR_JOINING ? "joining"
                                            : st.state == NETMGR_AP      ? "ap"
                                                                         : "off");
        cJSON_AddStringToObject(o, "ssid", st.ssid);
        cJSON_AddStringToObject(o, "ip", st.ip);
        cJSON_AddNumberToObject(o, "rssi", st.rssi);
        cJSON_AddBoolToObject(o, "ap_on", st.ap_on);
        cJSON *test = cJSON_AddObjectToObject(o, "test");
        cJSON_AddStringToObject(test, "ssid", st.test_ssid);
        cJSON_AddStringToObject(test, "result", netmgr_test_name(st.test));
        return send_cjson(req, 200, o);
    }
    if (req->method == HTTP_POST) {
        if (!read_body(req)) {
            return send_error(req, 413, "request too large");
        }
        cJSON *in = cJSON_Parse(s_body);
        const char *ssid = json_string(in, "ssid"), *pass = json_string(in, "password");
        const cJSON *test = cJSON_GetObjectItemCaseSensitive(in, "test");
        esp_err_t err;
        if (!netmgr_net_valid(ssid, pass ? pass : "")) {
            err = send_error(req, 400, "a name of 1-32 bytes and a password of 8-63 characters (or none)");
        } else if (cJSON_IsFalse(test)) {
            err = netmgr_add(ssid, pass ? pass : "") == ESP_OK ? send_json(req, 200, "{\"result\":\"saved\"}")
                                                                 : send_error(req, 500, "couldn't save it");
        } else if (netmgr_test_start(ssid, pass ? pass : "") != ESP_OK) {
            err = send_error(req, 409, "a test is running already");
        } else { /* spec §10.2; GET /api/wifi/networks reports the result, as the phone may drop off meanwhile */
            err = send_json(req, 202, "{\"result\":\"testing\"}");
        }
        memset(s_body, 0, req->content_len);
        cJSON_Delete(in);
        return err;
    }
    if (req->method == HTTP_DELETE) {
        char raw[3 * NETMGR_SSID_MAX + 1], ssid[NETMGR_SSID_MAX + 1];
        if (query == NULL || httpd_query_key_value(query, "ssid", raw, sizeof(raw)) != ESP_OK ||
            webui_url_decode(raw, ssid, sizeof(ssid)) < 0) {
            return send_error(req, 400, "which network? ?ssid=...");
        }
        return netmgr_forget(ssid) == ESP_OK ? send_json(req, 200, "{\"ok\":true}")
                                             : send_error(req, 404, "not a saved network");
    }
    return send_error(req, 405, "not for this method");
}

/* A firmware image (spec §10.5), checked for this project and chip before it is written through. */
static esp_err_t ota_upload(httpd_req_t *req)
{
    if (req->method != HTTP_POST) {
        return send_error(req, 405, "use POST");
    }
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        return send_error(req, 500, "no update partition");
    }
    const size_t head = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (req->content_len < head || req->content_len > next->size) {
        return send_error(req, 413, "not a firmware image of a fitting size");
    }
    size_t got = 0, filled = 0;
    while (filled < head) { /* the image header and the app description first */
        int n = httpd_req_recv(req, (char *)s_out + filled, OTA_CHUNK - filled);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            return send_error(req, 400, "the upload broke off");
        }
        filled += (size_t)n;
    }
    const esp_image_header_t *image = (const esp_image_header_t *)s_out;
    const esp_app_desc_t *desc = (const esp_app_desc_t *)(s_out + sizeof(esp_image_header_t) +
                                                          sizeof(esp_image_segment_header_t));
    const esp_app_desc_t *self = esp_app_get_description();
    if (image->magic != ESP_IMAGE_HEADER_MAGIC || image->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID ||
        desc->magic_word != ESP_APP_DESC_MAGIC_WORD || strncmp(desc->project_name, self->project_name,
                                                               sizeof(desc->project_name)) != 0 ||
        desc->version[0] == '\0') {
        return send_error(req, 400, "not a reflbo firmware for this chip");
    }
    char version[sizeof(desc->version) + 1];
    snprintf(version, sizeof(version), "%.*s", (int)sizeof(desc->version), desc->version);
    ESP_LOGI(TAG, "firmware upload: %s -> %s, %u bytes into %s", self->version, version,
             (unsigned)req->content_len, next->label);
    esp_ota_handle_t ota;
    if (esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        return send_error(req, 500, "the update partition won't open");
    }
    esp_err_t err = ESP_OK;
    while (err == ESP_OK) {
        err = esp_ota_write(ota, s_out, filled);
        got += filled;
        filled = 0;
        if (got >= req->content_len) {
            break;
        }
        int n = httpd_req_recv(req, (char *)s_out, OTA_CHUNK < req->content_len - got ? OTA_CHUNK
                                                                                      : req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        filled = (size_t)n;
        touch(); /* a long upload is activity too */
    }
    if (err != ESP_OK) {
        esp_ota_abort(ota);
        return send_error(req, 400, "the upload broke off");
    }
    err = esp_ota_end(ota); /* checks the image's own checksum and hash */
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(next);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "firmware rejected: %s", esp_err_to_name(err));
        return send_error(req, 400, "the image is damaged");
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "version", version);
    esp_err_t sent = send_cjson(req, 200, o);
    s_cfg.event(WEBUI_EVENT_UPDATED); /* after the reply: the app restarts in a moment */
    return sent;
}

static esp_err_t ota_status(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition(), *bad = esp_ota_get_last_invalid_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &state);
    const esp_app_desc_t *self = esp_app_get_description();
    char elf[17];
    esp_app_get_elf_sha256(elf, sizeof(elf));
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "version", self->version);
    cJSON_AddStringToObject(o, "elf", elf);
    cJSON_AddStringToObject(o, "built", self->date);
    cJSON_AddStringToObject(o, "partition", running->label);
    cJSON_AddBoolToObject(o, "pending", state == ESP_OTA_IMG_PENDING_VERIFY);
    cJSON_AddStringToObject(o, "rolled_back_from", bad != NULL ? bad->label : "");
    return send_cjson(req, 200, o);
}

typedef struct {
    const char *method, *path, *query, *body;
    webui_reply_t reply;
} api_call_t;

static void api_on_app(void *arg)
{
    api_call_t *c = arg;
    s_cfg.api(c->method, c->path, c->query, c->body, s_out, WEBUI_REPLY_MAX, &c->reply);
}

static const char *method_name(int method)
{
    switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    default: return "?";
    }
}

static esp_err_t api_handler(httpd_req_t *req)
{
    touch();
    char path[64], query[160];
    size_t len = strcspn(req->uri, "?");
    snprintf(path, sizeof(path), "%.*s", (int)(len < sizeof(path) - 1 ? len : sizeof(path) - 1), req->uri);
    bool has_query = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK;
    bool mutating = req->method != HTTP_GET;
    if (mutating && strcmp(path, "/api/ota") != 0) { /* spec §10.4: no cross-site form posts */
        char type[64];
        if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
            !webui_is_json_type(type)) {
            return send_error(req, 415, "send application/json");
        }
    }
    if (strncmp(path, "/api/auth", 9) == 0) {
        return auth_routes(req, path);
    }
    if (!session_ok(req)) {
        return send_error(req, 401, webui_password_set() ? "log in first" : "choose a password first");
    }
    if (strcmp(path, "/api/ota") == 0) {
        return ota_upload(req);
    }
    if (strcmp(path, "/api/ota/status") == 0) {
        return ota_status(req);
    }
    if (strncmp(path, "/api/wifi/", 10) == 0) {
        return wifi_routes(req, path, has_query ? query : NULL);
    }
    static const struct {
        const char *path;
        webui_event_t event;
    } k_events[] = { { "/api/done", WEBUI_EVENT_DONE },
                     { "/api/reboot", WEBUI_EVENT_REBOOT },
                     { "/api/factory-reset", WEBUI_EVENT_FACTORY_RESET } };
    for (size_t i = 0; i < sizeof(k_events) / sizeof(k_events[0]); i++) {
        if (strcmp(path, k_events[i].path) == 0) {
            if (req->method != HTTP_POST) {
                return send_error(req, 405, "use POST");
            }
            esp_err_t sent = send_json(req, 200, "{\"ok\":true}");
            s_cfg.event(k_events[i].event); /* after the reply */
            return sent;
        }
    }
    if (mutating && !read_body(req)) {
        return send_error(req, 413, "request too large");
    }
    api_call_t call = { .method = method_name(req->method), .path = path, .query = has_query ? query : "",
                        .body = mutating ? s_body : "", .reply = { .status = 404, .type = "application/json" } };
    if (s_cfg.run(api_on_app, &call) != ESP_OK) {
        return send_error(req, 503, "the device is busy");
    }
    httpd_resp_set_status(req, status_line(call.reply.status));
    httpd_resp_set_type(req, call.reply.type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)s_out, (ssize_t)call.reply.len);
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    if (!for_us(req)) {
        return peer_on_ap(req) ? to_portal(req) : httpd_resp_send_404(req);
    }
    touch(); /* our own pages count as use; a phone's connectivity probes don't */
    const asset_t *a = req->user_ctx;
    httpd_resp_set_type(req, a->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)a->start, a->end - a->start);
}

/* Anything else: an unknown page of ours goes to the start page, and on the AP a probe for
 * another host goes to the portal (spec §10.1). */
static esp_err_t fallback_handler(httpd_req_t *req)
{
    if (for_us(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, NULL, 0);
    }
    return peer_on_ap(req) ? to_portal(req) : httpd_resp_send_404(req);
}

esp_err_t webui_start(const webui_config_t *config)
{
    if (s_server != NULL || s_stopping) {
        return s_stopping ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    s_cfg = *config;
    if (s_auth_lock == NULL) {
        s_auth_lock = xSemaphoreCreateMutex();
    }
    s_body = heap_caps_malloc(WEBUI_BODY_MAX + 1, MALLOC_CAP_SPIRAM);
    s_out = heap_caps_malloc(WEBUI_REPLY_MAX, MALLOC_CAP_SPIRAM);
    if (s_auth_lock == NULL || s_body == NULL || s_out == NULL) {
        free(s_body);
        free(s_out);
        s_body = NULL;
        s_out = NULL;
        return ESP_ERR_NO_MEM;
    }
    webui_sessions_clear(&s_sessions); /* sessions end with config mode */
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.uri_match_fn = httpd_uri_match_wildcard;
    c.max_uri_handlers = 8;
    c.max_open_sockets = 7; /* with its 3 internal ones, within CONFIG_LWIP_MAX_SOCKETS (16) */
    c.stack_size = SERVER_STACK;
    c.task_priority = SERVER_PRIORITY;
    c.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &c);
    if (err != ESP_OK) {
        free(s_body);
        free(s_out);
        s_body = NULL;
        s_out = NULL;
        return err;
    }
    for (size_t i = 0; i < sizeof(k_assets) / sizeof(k_assets[0]); i++) {
        httpd_uri_t uri = { .uri = k_assets[i].uri, .method = HTTP_GET, .handler = asset_handler,
                            .user_ctx = (void *)&k_assets[i] };
        httpd_register_uri_handler(s_server, &uri);
    }
    httpd_uri_t api = { .uri = "/api/*", .method = HTTP_ANY, .handler = api_handler };
    httpd_register_uri_handler(s_server, &api);
    httpd_uri_t fallback = { .uri = "/*", .method = HTTP_GET, .handler = fallback_handler };
    httpd_register_uri_handler(s_server, &fallback);
    touch();
    ESP_LOGI(TAG, "web configurator up");
    return ESP_OK;
}

static void stopper(void *arg)
{
    httpd_stop((httpd_handle_t)arg);
    webui_sessions_clear(&s_sessions);
    free(s_body);
    free(s_out);
    s_body = NULL;
    s_out = NULL;
    s_stopping = false;
    ESP_LOGI(TAG, "web configurator down");
    vTaskDelete(NULL);
}

void webui_stop(void)
{
    if (s_server == NULL || s_stopping) {
        return;
    }
    /* Not on the calling task: a request may be waiting for the app task, which is likely the
     * caller, and httpd_stop() waits for that request. */
    s_stopping = true;
    httpd_handle_t server = s_server;
    s_server = NULL;
    if (xTaskCreatePinnedToCore(stopper, "webui_stop", STOPPER_STACK, server, SERVER_PRIORITY, NULL,
                                tskNO_AFFINITY) != pdPASS) {
        httpd_stop(server); /* no memory for a task: stop here and hope nothing waits */
        s_stopping = false;
    }
}

bool webui_running(void)
{
    return s_server != NULL;
}

int64_t webui_last_request_ms(void)
{
    return s_last_ms;
}
