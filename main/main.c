#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "coap3/coap.h"
#include "lwip/netdb.h"
#include "wifi.h"
#include <string.h>
#include <stdlib.h>

#define COAP_HOST           "coap.thingsboard.cloud"
#define COAP_PORT           5683
#define PUBLISH_INTERVAL_MS (3 * 60 * 1000)  // 3분

#define LED_GPIO            GPIO_NUM_4

static const char *TAG = "COAP";
static char s_token[32];

// ── LED ──────────────────────────────────────────────────────────────────────

static void led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_GPIO, 0);
}

static void led_control(const char *data, int data_len)
{
    if (strnstr(data, "\"state\":\"on\"", data_len) != NULL) {
        gpio_set_level(LED_GPIO, 1);
        ESP_LOGI(TAG, "LED ON");
    } else if (strnstr(data, "\"state\":\"off\"", data_len) != NULL) {
        gpio_set_level(LED_GPIO, 0);
        ESP_LOGI(TAG, "LED OFF");
    } else {
        ESP_LOGW(TAG, "Unknown LED state");
    }
}

// ── CoAP PDU 헬퍼 ─────────────────────────────────────────────────────────────

static void add_tb_uri_path(coap_optlist_t **optlist, const char *endpoint)
{
    coap_insert_optlist(optlist,
        coap_new_optlist(COAP_OPTION_URI_PATH, 3, (uint8_t *)"api"));
    coap_insert_optlist(optlist,
        coap_new_optlist(COAP_OPTION_URI_PATH, 2, (uint8_t *)"v1"));
    coap_insert_optlist(optlist,
        coap_new_optlist(COAP_OPTION_URI_PATH, strlen(s_token), (uint8_t *)s_token));
    coap_insert_optlist(optlist,
        coap_new_optlist(COAP_OPTION_URI_PATH, strlen(endpoint), (uint8_t *)endpoint));
}

static void add_json_content_format(coap_pdu_t *pdu)
{
    unsigned char buf[4];
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(buf, sizeof(buf),
                                        COAP_MEDIATYPE_APPLICATION_JSON), buf);
}

// ── RPC 응답 전송 ──────────────────────────────────────────────────────────────

static void send_rpc_response(coap_session_t *session, int req_id)
{
    coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST,
                                     coap_new_message_id(session),
                                     coap_session_max_pdu_size(session));
    if (!pdu) return;

    coap_optlist_t *optlist = NULL;
    add_tb_uri_path(&optlist, "rpc");
    char id_str[12];
    int id_len = snprintf(id_str, sizeof(id_str), "%d", req_id);
    coap_insert_optlist(&optlist,
        coap_new_optlist(COAP_OPTION_URI_PATH, id_len, (uint8_t *)id_str));
    coap_add_optlist_pdu(pdu, &optlist);
    coap_delete_optlist(optlist);

    add_json_content_format(pdu);

    const char *resp = "{\"result\":\"ok\"}";
    coap_add_data(pdu, strlen(resp), (const uint8_t *)resp);

    coap_send(session, pdu);
    ESP_LOGI(TAG, "RPC response sent -> id=%d", req_id);
}

// ── CoAP 응답 핸들러 ───────────────────────────────────────────────────────────

static coap_response_t coap_response_handler(coap_session_t *session,
                                              const coap_pdu_t *sent,
                                              const coap_pdu_t *received,
                                              const coap_mid_t mid)
{
    coap_opt_iterator_t opt_iter;
    coap_opt_t *observe_opt = coap_check_option(received, COAP_OPTION_OBSERVE, &opt_iter);

    if (observe_opt) {
        // RPC Observe 알림
        size_t data_len;
        const uint8_t *data;
        if (coap_get_data(received, &data_len, &data)) {
            ESP_LOGI(TAG, "RPC received: %.*s", (int)data_len, (char *)data);

            if (strnstr((char *)data, "\"setLed\"", data_len)) {
                led_control((char *)data, data_len);
            }

            // id 추출 후 응답
            const char *id_ptr = strnstr((char *)data, "\"id\":", data_len);
            if (id_ptr) {
                id_ptr += 5;
                while (*id_ptr == ' ') id_ptr++;
                send_rpc_response(session, atoi(id_ptr));
            }
        }
    }
    return COAP_RESPONSE_OK;
}

// ── Telemetry 전송 ─────────────────────────────────────────────────────────────

static void send_telemetry(coap_session_t *session, int value, int count)
{
    coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST,
                                     coap_new_message_id(session),
                                     coap_session_max_pdu_size(session));
    if (!pdu) return;

    coap_optlist_t *optlist = NULL;
    add_tb_uri_path(&optlist, "telemetry");
    coap_add_optlist_pdu(pdu, &optlist);
    coap_delete_optlist(optlist);

    add_json_content_format(pdu);

    char payload[64];
    int plen = snprintf(payload, sizeof(payload), "{\"value\":%d,\"count\":%d}", value, count);
    coap_add_data(pdu, plen, (const uint8_t *)payload);

    coap_send(session, pdu);
    ESP_LOGI(TAG, "Telemetry sent -> %s", payload);
}

// ── RPC Observe 등록 ───────────────────────────────────────────────────────────

static void register_rpc_observe(coap_session_t *session)
{
    coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET,
                                     coap_new_message_id(session),
                                     coap_session_max_pdu_size(session));
    if (!pdu) return;

    coap_optlist_t *optlist = NULL;
    add_tb_uri_path(&optlist, "rpc");
    coap_add_optlist_pdu(pdu, &optlist);
    coap_delete_optlist(optlist);

    unsigned char buf[4];
    coap_add_option(pdu, COAP_OPTION_OBSERVE,
                    coap_encode_var_safe(buf, sizeof(buf), COAP_OBSERVE_ESTABLISH), buf);

    coap_send(session, pdu);
    ESP_LOGI(TAG, "RPC observe registered");
}

// ── CoAP 메인 태스크 ───────────────────────────────────────────────────────────

static void coap_task(void *arg)
{
    const char *token = wifi_get_tb_token();
    if (strlen(token) == 0) {
        ESP_LOGE(TAG, "Access token not set. Configure via http://192.168.4.1");
        vTaskDelete(NULL);
        return;
    }
    strncpy(s_token, token, sizeof(s_token) - 1);
    s_token[sizeof(s_token) - 1] = '\0';
    ESP_LOGI(TAG, "Token: %s", s_token);

    // DNS 조회 (getaddrinfo, 최대 3회 재시도, 2초 간격)
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", COAP_PORT);

    for (int retry = 0; retry < 3 && res == NULL; retry++) {
        if (retry > 0) vTaskDelay(pdMS_TO_TICKS(2000));
        int err = getaddrinfo(COAP_HOST, port_str, &hints, &res);
        if (err != 0 || res == NULL) {
            ESP_LOGW(TAG, "DNS resolve failed, retry %d/3 (err=%d)", retry + 1, err);
            res = NULL;
        }
    }
    if (res == NULL) {
        ESP_LOGE(TAG, "DNS resolve failed for %s after 3 attempts", COAP_HOST);
        vTaskDelete(NULL);
        return;
    }

    coap_address_t dst_addr;
    coap_address_init(&dst_addr);
    dst_addr.addr.sin = *(struct sockaddr_in *)res->ai_addr;
    dst_addr.size = sizeof(struct sockaddr_in);
    freeaddrinfo(res);
    ESP_LOGI(TAG, "DNS resolved: %s", COAP_HOST);

    // 컨텍스트 / 세션 생성
    coap_context_t *ctx = coap_new_context(NULL);
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to create CoAP context");
        vTaskDelete(NULL);
        return;
    }
    coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

    coap_session_t *session = coap_new_client_session(
        ctx, NULL, &dst_addr, COAP_PROTO_UDP);
    if (!session) {
        ESP_LOGE(TAG, "Failed to create CoAP session");
        coap_free_context(ctx);
        vTaskDelete(NULL);
        return;
    }

    coap_register_response_handler(ctx, coap_response_handler);
    ESP_LOGI(TAG, "CoAP session ready -> %s:%d", COAP_HOST, COAP_PORT);

    // RPC Observe 등록
    register_rpc_observe(session);

    // 메인 루프: 첫 Telemetry 즉시 전송 후 3분 주기
    int count = 1;
    TickType_t last_telemetry = xTaskGetTickCount() - pdMS_TO_TICKS(PUBLISH_INTERVAL_MS);

    while (1) {
        coap_io_process(ctx, 1000);  // 최대 1초 대기 (RPC 수신 처리)

        if (xTaskGetTickCount() - last_telemetry >= pdMS_TO_TICKS(PUBLISH_INTERVAL_MS)) {
            int value = (int)(esp_random() % 100);
            send_telemetry(session, value, count++);
            last_telemetry = xTaskGetTickCount();
        }
    }
}

static void coap_start(void)
{
    xTaskCreate(coap_task, "coap_task", 8192, NULL, 5, NULL);
}

void app_main(void)
{
    led_init();
    wifi_init(coap_start);
}
