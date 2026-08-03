#include "wifi_link.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#define WIFI_SSID       "AFSK-TRX"
#define WIFI_PASS       "afsk12345"
#define WIFI_CHANNEL    1
#define WIFI_MAX_STA    4
#define WIFI_INACTIVE_TIME_S 30

#define TX_QUEUE_LEN    4
#define POST_BUF_SIZE   1024
#define WS_MAX_CLIENTS  4

static const char *TAG = "WIFI_LINK";

static QueueHandle_t s_tx_queue = NULL;
static httpd_handle_t s_http_server = NULL;
static httpd_handle_t s_ws_server = NULL;
static SemaphoreHandle_t s_ws_mutex = NULL;

QueueHandle_t wifi_link_get_tx_queue(void)
{
    return s_tx_queue;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void url_decode(char *out, size_t out_size, const char *in, const char *end)
{
    size_t i = 0;
    while (i < out_size - 1 && in && *in && in < end) {
        if (*in == '+') {
            out[i++] = ' ';
        } else if (*in == '%' && (in + 2) < end &&
                   hex_val(in[1]) >= 0 && hex_val(in[2]) >= 0) {
            out[i++] = (char)((hex_val(in[1]) << 4) | hex_val(in[2]));
            in += 2;
        } else {
            out[i++] = *in;
        }
        in++;
    }
    out[i] = '\0';
}

void wifi_link_broadcast(const char *from, const char *text)
{
    if (!s_ws_server || !from || !text) {
        return;
    }

    size_t from_len = strlen(from);
    size_t text_len = strlen(text);
    if (from_len == 0 || text_len == 0) {
        return;
    }

    size_t payload_len = from_len + 1 + text_len;
    char *payload = (char *)malloc(payload_len + 1);
    if (!payload) {
        return;
    }
    memcpy(payload, from, from_len);
    payload[from_len] = ':';
    memcpy(payload + from_len + 1, text, text_len);
    payload[payload_len] = '\0';

    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t *)payload;
    ws_pkt.len = payload_len;

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    size_t max_clients = WS_MAX_CLIENTS;
    int client_fds[WS_MAX_CLIENTS];
    if (httpd_get_client_list(s_ws_server, &max_clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < max_clients; i++) {
            esp_err_t ret = httpd_ws_send_frame_async(s_ws_server, client_fds[i], &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "WS send to fd %d failed: %d", client_fds[i], ret);
            }
        }
    }
    xSemaphoreGive(s_ws_mutex);

    free(payload);
}

static esp_err_t ping_get_handler(httpd_req_t *req)
{
    const char *resp = "pong";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t send_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    size_t body_len = req->content_len;
    if (body_len > POST_BUF_SIZE - 1) {
        body_len = POST_BUF_SIZE - 1;
    }

    char *body = (char *)malloc(POST_BUF_SIZE);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int total = 0;
    while (total < (int)body_len) {
        int ret = httpd_req_recv(req, body + total, body_len - total);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            break;
        }
        total += ret;
    }
    body[total] = '\0';

    esp_err_t result = ESP_FAIL;
    char from[64] = {0};
    char text[POST_BUF_SIZE] = {0};

    char *p_from = strstr(body, "from=");
    if (!p_from) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'from'");
        goto cleanup;
    }
    p_from += 5;

    char *p_from_end = strchr(p_from, '&');
    if (!p_from_end) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'text'");
        goto cleanup;
    }

    char *p_text = strstr(p_from_end, "text=");
    if (!p_text) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'text'");
        goto cleanup;
    }
    p_text += 5;
    const char *p_text_end = body + total;

    url_decode(from, sizeof(from), p_from, p_from_end);
    url_decode(text, sizeof(text), p_text, p_text_end);

    if (s_tx_queue && strlen(text) > 0) {
        char *msg = strdup(text);
        if (msg) {
            if (xQueueSend(s_tx_queue, &msg, pdMS_TO_TICKS(100)) != pdPASS) {
                free(msg);
                ESP_LOGW(TAG, "TX queue full, message dropped");
            }
        }
    }

    wifi_link_broadcast(from, text);

    const char *resp = "OK";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, strlen(resp));
    result = ESP_OK;

cleanup:
    free(body);
    return result;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS handshake done, fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // Увеличили буфер для приема сообщений (имя + текст)
    uint8_t buf[256] = {0};
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = buf;
    ws_pkt.len = 0;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS recv failed: %d", ret);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len > 0) {
        ws_pkt.payload[ws_pkt.len] = '\0';
        ESP_LOGI(TAG, "WS text from fd %d: %s", httpd_req_to_sockfd(req), ws_pkt.payload);

        // Обработка смены имени
        if (strncmp((const char *)ws_pkt.payload, "setName:", 8) == 0) {
            ESP_LOGI(TAG, "Client %d set name to %s", httpd_req_to_sockfd(req), ws_pkt.payload + 8);
        } 
        // НОВАЯ ОБРАБОТКА: Прием сообщения от клиента
        else if (strncmp((const char *)ws_pkt.payload, "msg:", 4) == 0) {
            char *payload = (char *)ws_pkt.payload + 4;
            char *from = payload;
            char *colon = strchr(payload, ':');
            
            if (colon) {
                *colon = '\0'; // Разделяем строку на "от кого" и "текст"
                char *text = colon + 1;

                // Кладем в очередь на передачу в эфир
                if (s_tx_queue && strlen(text) > 0) {
                    char *msg = strdup(text);
                    if (msg) {
                        if (xQueueSend(s_tx_queue, &msg, pdMS_TO_TICKS(100)) != pdPASS) {
                            free(msg);
                            ESP_LOGW(TAG, "TX queue full, WS message dropped");
                        }
                    }
                }
                // Рассылаем всем клиентам (включая отправителя, чтобы он увидел в чате)
                wifi_link_broadcast(from, text);
            } else {
                ESP_LOGW(TAG, "Malformed msg frame, no name separator: %s",
                         ws_pkt.payload);
            }
        }
    }

    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station "MACSTR" connected, AID=%d", MAC2STR(evt->mac), evt->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station "MACSTR" disconnected, AID=%d", MAC2STR(evt->mac), evt->aid);
    }
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.core_id = 0; 
    config.max_open_sockets = 4;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&s_http_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return ESP_FAIL;
    }

    httpd_uri_t ping_uri = {
        .uri = "/ping",
        .method = HTTP_GET,
        .handler = ping_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t send_uri = {
        .uri = "/send",
        .method = HTTP_POST,
        .handler = send_post_handler,
        .user_ctx = NULL,
    };

    httpd_register_uri_handler(s_http_server, &ping_uri);
    httpd_register_uri_handler(s_http_server, &send_uri);

    ESP_LOGI(TAG, "HTTP server started on port 80 (Core 0)");
    return ESP_OK;
}

static esp_err_t start_ws_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32769;
    config.core_id = 0; 
    config.max_open_sockets = WS_MAX_CLIENTS;
    config.max_uri_handlers = 2;
    config.lru_purge_enable = true;

    if (httpd_start(&s_ws_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "WS server start failed");
        return ESP_FAIL;
    }

    httpd_uri_t ws_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
#ifdef CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
        .handle_ws_control_frames = false,
#endif
    };

    httpd_register_uri_handler(s_ws_server, &ws_uri);

    ESP_LOGI(TAG, "WS server started on port 81 (Core 0)");
    return ESP_OK;
}

void wifi_link_init(void)
{
    s_tx_queue = xQueueCreate(TX_QUEUE_LEN, sizeof(char *));
    if (!s_tx_queue) {
        ESP_LOGE(TAG, "Failed to create TX queue");
        return;
    }

    s_ws_mutex = xSemaphoreCreateMutex();
    if (!s_ws_mutex) {
        ESP_LOGE(TAG, "Failed to create WS mutex");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL, NULL));

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.ap.ssid, WIFI_SSID, strlen(WIFI_SSID));
    wifi_config.ap.ssid_len = strlen(WIFI_SSID);
    memcpy(wifi_config.ap.password, WIFI_PASS, strlen(WIFI_PASS));
    wifi_config.ap.channel = WIFI_CHANNEL;
    wifi_config.ap.max_connection = WIFI_MAX_STA;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_AP, WIFI_INACTIVE_TIME_S));

    ESP_LOGI(TAG, "Wi-Fi AP started: SSID=%s, IP=192.168.4.1", WIFI_SSID);

    if (start_http_server() != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed to start");
    }
    if (start_ws_server() != ESP_OK) {
        ESP_LOGE(TAG, "WS server failed to start");
    }
}
