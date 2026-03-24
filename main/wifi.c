#include "wifi.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include <string.h>
#include <stdlib.h>

#define AP_SSID      "ESP32-Config"
#define AP_CHANNEL   1
#define AP_MAX_CONN  4
#define NUM_CREDS    3

#define NVS_NAMESPACE "wifi_cfg"

static const char *TAG = "WIFI";

typedef struct { char ssid[33]; char pass[65]; } cred_t;
static cred_t            s_creds[NUM_CREDS];
static char              s_tb_token[32];
static int               s_try_idx    = 0;
static wifi_connected_cb_t s_connected_cb = NULL;
static bool              s_coap_started = false;

// ── NVS ──────────────────────────────────────────────────────────────────────

static void nvs_load_all(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    char key[8];
    for (int i = 0; i < NUM_CREDS; i++) {
        size_t sl = sizeof(s_creds[i].ssid), pl = sizeof(s_creds[i].pass);
        snprintf(key, sizeof(key), "ssid%d", i + 1);
        if (nvs_get_str(h, key, s_creds[i].ssid, &sl) != ESP_OK) s_creds[i].ssid[0] = '\0';
        snprintf(key, sizeof(key), "pass%d", i + 1);
        if (nvs_get_str(h, key, s_creds[i].pass, &pl) != ESP_OK) s_creds[i].pass[0] = '\0';
    }
    size_t tl = sizeof(s_tb_token);
    if (nvs_get_str(h, "tb_token", s_tb_token, &tl) != ESP_OK) s_tb_token[0] = '\0';
    nvs_close(h);
}

static void nvs_save_all(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    char key[8];
    for (int i = 0; i < NUM_CREDS; i++) {
        snprintf(key, sizeof(key), "ssid%d", i + 1);
        nvs_set_str(h, key, s_creds[i].ssid);
        snprintf(key, sizeof(key), "pass%d", i + 1);
        nvs_set_str(h, key, s_creds[i].pass);
    }
    nvs_set_str(h, "tb_token", s_tb_token);
    nvs_commit(h);
    nvs_close(h);
}

// ── STA 연결 ─────────────────────────────────────────────────────────────────

static void try_connect(void)
{
    for (int i = 0; i < NUM_CREDS; i++) {
        int idx = (s_try_idx + i) % NUM_CREDS;
        if (strlen(s_creds[idx].ssid) == 0) continue;
        s_try_idx = (idx + 1) % NUM_CREDS;
        wifi_config_t cfg = {0};
        strncpy((char *)cfg.sta.ssid,     s_creds[idx].ssid, sizeof(cfg.sta.ssid)     - 1);
        strncpy((char *)cfg.sta.password, s_creds[idx].pass, sizeof(cfg.sta.password) - 1);
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        esp_wifi_connect();
        ESP_LOGI(TAG, "Trying '%s'...", s_creds[idx].ssid);
        return;
    }
    ESP_LOGW(TAG, "No credentials available");
}

// ── URL 디코딩 ────────────────────────────────────────────────────────────────

static void url_decode(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (*src && i < max - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void parse_field(const char *body, const char *key, char *out, size_t max)
{
    char search[16];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= sizeof((char[256]){})) len = 255;
    char raw[256] = {0};
    memcpy(raw, p, len);
    url_decode(out, raw, max);
}

// ── HTML 특수문자 이스케이프 ───────────────────────────────────────────────────

static void html_esc(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (*src && i < max - 7) {
        switch (*src) {
            case '"': memcpy(dst + i, "&quot;", 6); i += 6; break;
            case '<': memcpy(dst + i, "&lt;",   4); i += 4; break;
            case '>': memcpy(dst + i, "&gt;",   4); i += 4; break;
            case '&': memcpy(dst + i, "&amp;",  5); i += 5; break;
            default:  dst[i++] = *src; break;
        }
        src++;
    }
    dst[i] = '\0';
}

// ── HTTP 핸들러 ───────────────────────────────────────────────────────────────

static const char HTML_HEAD[] =
    "<!DOCTYPE html><html lang='ko'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 WiFi \354\204\244\354\240\225</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
         "background:#eef0f7;min-height:100vh;display:flex;align-items:center;"
         "justify-content:center;padding:16px}"
    ".card{background:#fff;border-radius:20px;box-shadow:0 8px 32px rgba(80,60,180,.13);"
           "width:100%;max-width:460px;overflow:hidden}"
    ".hdr{background:linear-gradient(135deg,#5c6bc0 0%,#7c4dff 100%);"
          "padding:28px 24px 22px;color:#fff;text-align:center}"
    ".hdr .icon{font-size:36px;margin-bottom:8px}"
    ".hdr h1{font-size:20px;font-weight:700;letter-spacing:.3px}"
    ".hdr p{font-size:12px;opacity:.82;margin-top:5px}"
    ".body{padding:22px}"
    ".net{background:#f7f8fc;border:1.5px solid #e3e6f3;border-radius:14px;"
          "padding:16px 16px 10px;margin-bottom:14px;transition:border-color .2s}"
    ".net:focus-within{border-color:#7c4dff;box-shadow:0 0 0 3px rgba(124,77,255,.08)}"
    ".badge{display:inline-flex;align-items:center;gap:6px;font-size:11px;"
            "font-weight:700;color:#5c6bc0;letter-spacing:.6px;text-transform:uppercase;"
            "background:#ede7ff;border-radius:20px;padding:3px 10px;margin-bottom:12px}"
    ".fld{margin-bottom:10px}"
    ".fld label{display:block;font-size:12px;color:#666;font-weight:600;margin-bottom:4px}"
    ".wrap{position:relative;display:flex;align-items:center}"
    ".wrap input{flex:1;padding:10px 42px 10px 12px;border:1.5px solid #dde0ed;"
                 "border-radius:9px;font-size:14px;outline:none;background:#fff;"
                 "transition:border-color .2s,box-shadow .2s;color:#333}"
    ".wrap input:focus{border-color:#7c4dff;box-shadow:0 0 0 3px rgba(124,77,255,.1)}"
    ".eye{position:absolute;right:10px;background:none;border:none;cursor:pointer;"
          "color:#aaa;font-size:17px;padding:2px;line-height:1;transition:color .15s}"
    ".eye:hover{color:#7c4dff}"
    ".btn{width:100%;padding:13px;background:linear-gradient(135deg,#5c6bc0,#7c4dff);"
          "color:#fff;border:none;border-radius:11px;font-size:15px;font-weight:700;"
          "cursor:pointer;letter-spacing:.3px;transition:opacity .2s,transform .1s;"
          "margin-top:6px}"
    ".btn:hover{opacity:.92}"
    ".btn:active{transform:scale(.98)}"
    ".note{text-align:center;font-size:11px;color:#aaa;margin-top:12px;line-height:1.6}"
    "</style></head><body><div class='card'>"
    "<div class='hdr'><div class='icon'>&#x1F4F6;</div>"
    "<h1>ESP32 WiFi \354\204\244\354\240\225</h1>"
    "<p>\354\265\234\353\214\200 3\352\260\234\354\235\230 \353\204\244\355\212\270\354\233\214\355\201\254\353\245\274 \354\240\200\354\236\245\355\225\240 \354\210\230 \354\236\210\354\212\265\353\213\210\353\213\244</p>"
    "</div>"
    "<form method='POST' action='/save'><div class='body'>";

static const char HTML_NET_FMT[] =
    "<div class='net'>"
    "<span class='badge'>%s</span>"
    "<div class='fld'><label>SSID</label>"
    "<div class='wrap'><input type='text' name='ssid%d' value='%s' placeholder='WiFi \354\235\264\353\246\204'></div></div>"
    "<div class='fld'><label>Password</label>"
    "<div class='wrap'>"
    "<input type='password' name='pass%d' id='p%d' value='%s' placeholder='\353\271\204\353\260\200\353\262\210\355\230\270'>"
    "<button type='button' class='eye' onclick='tog(\"p%d\",this)'>&#x1F441;</button>"
    "</div></div></div>";

static const char HTML_TOKEN_FMT[] =
    "<div class='net'>"
    "<span class='badge'>&#x1F511; ThingsBoard Access Token</span>"
    "<div class='fld'><label>Access Token</label>"
    "<div class='wrap'>"
    "<input type='password' name='tb_token' id='ptok' value='%s' placeholder='Device Access Token'>"
    "<button type='button' class='eye' onclick='tog(\"ptok\",this)'>&#x1F441;</button>"
    "</div></div></div>";

static const char HTML_FOOT[] =
    "<button class='btn' type='submit'>&#x1F4BE;&nbsp; \354\240\200\354\236\245 \353\260\217 \354\227\260\352\262\260</button>"
    "<p class='note'>"
    "\xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC 1\xEB\xB6\x80\xED\x84\xB0 \xEC\x88\x9C\xEC\x84\x9C\xEB\x8C\x80\xEB\xA1\x9C \xEC\x97\xB0\xEA\xB2\xB0\xEC\x9D\x84 \xEC\x8B\x9C\xEB\x8F\x84\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4<br>"
    "\xEC\x97\xB0\xEA\xB2\xB0 \xEC\x8B\xA4\xED\x8C\xA8 \xEC\x8B\x9C \xEB\x8B\xA4\xEC\x9D\x8C \xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC\xEB\xA1\x9C \xEC\x9E\x90\xEB\x8F\x99 \xEC\x8A\xA4\xEC\x9C\x84\xEC\xB9\x98\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4"
    "</p></div></form></div>"
    "<script>"
    "function tog(id,b){"
    "var e=document.getElementById(id);"
    "e.type=e.type==='password'?'text':'password';"
    "b.innerHTML=e.type==='password'?'&#x1F441;':'&#x1F648;';"
    "}"
    "</script></body></html>";

static const char *NET_LABELS[NUM_CREDS] = {
    "&#x2460; \xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC 1 &mdash; \xEC\x9A\xB0\xEC\x84\xA0\xEC\x88\x9C\xEC\x9C\x84 \xEB\x86\x92\xEC\x9D\x8C",
    "&#x2461; \xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC 2",
    "&#x2462; \xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC 3"
};

static esp_err_t handle_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HTML_HEAD, HTTPD_RESP_USE_STRLEN);

    char block[512];
    char se[80], pe[160];
    for (int i = 0; i < NUM_CREDS; i++) {
        html_esc(se, s_creds[i].ssid, sizeof(se));
        html_esc(pe, s_creds[i].pass, sizeof(pe));
        int n = snprintf(block, sizeof(block), HTML_NET_FMT,
                         NET_LABELS[i],
                         i + 1, se,
                         i + 1, i + 1, pe,
                         i + 1);
        httpd_resp_send_chunk(req, block, n);
    }

    char tok_esc[64];
    html_esc(tok_esc, s_tb_token, sizeof(tok_esc));
    char tok_block[512];
    int tn = snprintf(tok_block, sizeof(tok_block), HTML_TOKEN_FMT, tok_esc);
    httpd_resp_send_chunk(req, tok_block, tn);

    httpd_resp_send_chunk(req, HTML_FOOT, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_save(httpd_req_t *req)
{
    char *body = malloc(1024);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }

    int len = httpd_req_recv(req, body, 1023);
    if (len <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
    body[len] = '\0';

    char key_s[8], key_p[8];
    for (int i = 0; i < NUM_CREDS; i++) {
        snprintf(key_s, sizeof(key_s), "ssid%d", i + 1);
        snprintf(key_p, sizeof(key_p), "pass%d", i + 1);
        parse_field(body, key_s, s_creds[i].ssid, sizeof(s_creds[i].ssid));
        parse_field(body, key_p, s_creds[i].pass, sizeof(s_creds[i].pass));
    }
    parse_field(body, "tb_token", s_tb_token, sizeof(s_tb_token));
    free(body);

    nvs_save_all();
    for (int i = 0; i < NUM_CREDS; i++) {
        if (strlen(s_creds[i].ssid) > 0)
            ESP_LOGI(TAG, "Saved [%d] ssid='%s'", i + 1, s_creds[i].ssid);
    }

    static const char SAVED_HTML[] =
        "<!DOCTYPE html><html lang='ko'><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
             "background:#eef0f7;min-height:100vh;display:flex;align-items:center;"
             "justify-content:center;padding:16px}"
        ".card{background:#fff;border-radius:20px;box-shadow:0 8px 32px rgba(80,60,180,.13);"
               "padding:40px 32px;text-align:center;max-width:360px;width:100%%}"
        ".icon{font-size:52px;margin-bottom:16px}"
        "h2{font-size:18px;font-weight:700;color:#333;margin-bottom:8px}"
        "p{font-size:14px;color:#888;line-height:1.6;margin-bottom:20px}"
        "a{display:inline-block;padding:10px 24px;background:linear-gradient(135deg,#5c6bc0,#7c4dff);"
          "color:#fff;border-radius:9px;text-decoration:none;font-size:14px;font-weight:600}"
        "</style></head><body>"
        "<div class='card'>"
        "<div class='icon'>&#x2705;</div>"
        "<h2>\354\240\200\354\236\245 \354\231\204\353\243\214</h2>"
        "<p>WiFi \354\227\260\352\262\260\354\235\204 \354\213\234\353\217\204\355\225\251\353\213\210\353\213\244.</p>"
        "<a href='/'>\354\204\244\354\240\225 \355\216\230\354\235\264\354\247\200\353\241\234</a>"
        "</div></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);

    esp_wifi_disconnect();
    s_try_idx = 0;
    try_connect();
    return ESP_OK;
}

static void start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return;
    }
    httpd_uri_t get_uri  = { .uri = "/",     .method = HTTP_GET,  .handler = handle_get  };
    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = handle_save };
    httpd_register_uri_handler(server, &get_uri);
    httpd_register_uri_handler(server, &save_uri);
    ESP_LOGI(TAG, "HTTP server ready -> http://192.168.4.1");
}

// ── 이벤트 핸들러 ──────────────────────────────────────────────────────────────

static void event_handler(void *arg, esp_event_base_t base,
                           int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_coap_started = false;
        ESP_LOGW(TAG, "STA disconnected, trying next...");
        try_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        if (!s_coap_started && s_connected_cb) {
            s_coap_started = true;
            s_connected_cb();
        }
    }
}

// ── 공개 API ──────────────────────────────────────────────────────────────────

const char *wifi_get_tb_token(void)
{
    return s_tb_token;
}

void wifi_init(wifi_connected_cb_t on_connected)
{
    s_connected_cb = on_connected;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = strlen(AP_SSID),
            .channel        = AP_CHANNEL,
            .authmode       = WIFI_AUTH_OPEN,
            .max_connection = AP_MAX_CONN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    nvs_load_all();

    // STA에 첫 번째 유효한 자격증명 미리 설정
    for (int i = 0; i < NUM_CREDS; i++) {
        if (strlen(s_creds[i].ssid) > 0) {
            wifi_config_t sta_cfg = {0};
            strncpy((char *)sta_cfg.sta.ssid,     s_creds[i].ssid, sizeof(sta_cfg.sta.ssid)     - 1);
            strncpy((char *)sta_cfg.sta.password, s_creds[i].pass, sizeof(sta_cfg.sta.password) - 1);
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
            ESP_LOGI(TAG, "Loaded credentials for '%s'", s_creds[i].ssid);
            break;
        }
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP started -> SSID: '%s'", AP_SSID);

    start_http_server();

    // 저장된 자격증명이 있으면 연결 시도
    try_connect();
}
