#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_websocket_client.h"


#define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_USE_WSS                             0

#if EXAMPLE_USE_WSS == 1
extern const char server_ca_crt_start[]          asm("_binary_server_ca_crt_start");
extern const char server_ca_crt_end[]            asm("_binary_server_ca_crt_end");
extern const char client_crt_start[]             asm("_binary_client_crt_start");
extern const char client_crt_end[]               asm("_binary_client_crt_end");
extern const char client_key_start[]             asm("_binary_client_key_start");
extern const char client_key_end[]               asm("_binary_client_key_end");
#endif

static const char *TAG = "websocket_client";
static esp_websocket_client_handle_t hd_ws_client = NULL;

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ws_data = (esp_websocket_event_data_t *)event_data;
    char send_data[16] = {0};
    char recv_data[128] = {0};
    
    switch (event_id) {
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
        break;
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEFORE_CONNECT");
        break;
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        memset(send_data, 'a', sizeof(send_data) - 1);
        esp_websocket_client_send_text(hd_ws_client, send_data, strlen(send_data), 100);
        memset(send_data, 0x05, sizeof(send_data));
        esp_websocket_client_send_bin(hd_ws_client, send_data, sizeof(send_data), 100);
        memset(send_data, 0x11, sizeof(send_data));
        esp_websocket_client_send_bin_partial(hd_ws_client, send_data, sizeof(send_data), 100);
        memset(send_data, 0x22, sizeof(send_data));
        esp_websocket_client_send_cont_msg(hd_ws_client, send_data, sizeof(send_data), 100);
        memset(send_data, 0x33, sizeof(send_data));
        esp_websocket_client_send_cont_msg(hd_ws_client, send_data, sizeof(send_data), 100);
        esp_websocket_client_send_fin(hd_ws_client, 100);
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "recv ws frame, opcode(0-subpkg,1-text,2-hex,8-dis,9-ping,10-pong):%u data_len:%d payload_len:%d payload_offset:%d",
            ws_data->op_code, ws_data->data_len, ws_data->payload_len, ws_data->payload_offset);
        if (HTTPD_WS_TYPE_TEXT == ws_data->op_code) {
            memcpy(recv_data, ws_data->data_ptr, ws_data->data_len);
            recv_data[ws_data->data_len] = 0;
            ESP_LOGI(TAG, "%s", recv_data);
        } else if (HTTPD_WS_TYPE_BINARY == ws_data->op_code) {
            ESP_LOG_BUFFER_HEX(TAG, ws_data->data_ptr, ws_data->data_len);
        } else if (HTTPD_WS_TYPE_CLOSE == ws_data->op_code) {
            ESP_LOGI(TAG, "disconnect reason:%d", (ws_data->data_ptr[0] << 8) + ws_data->data_ptr[1]);
        } else if (HTTPD_WS_TYPE_PING == ws_data->op_code) {
            ESP_LOGI(TAG, "ping");
        } else if (HTTPD_WS_TYPE_PONG == ws_data->op_code) {
            ESP_LOGI(TAG, "pong");
        } else {
            ESP_LOGW(TAG, "unknown op_code:%u", ws_data->op_code);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CLOSED");
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
    default:
        ESP_LOGW(TAG, "unknown event_id:%ld", event_id);
        break;
    }
}

static void start_websocket_client()
{
    esp_websocket_client_config_t config = {0};
#if EXAMPLE_USE_WSS == 1
    config.uri = "wss://echo.websocket.events";
    config.transport = WEBSOCKET_TRANSPORT_OVER_SSL;
    config.skip_cert_common_name_check = true;
    config.cert_pem = server_ca_crt_start;
    config.client_cert = client_crt_start;
    config.client_cert_len = client_crt_end - client_crt_start;
    config.client_key = client_key_start;
    config.client_key_len = client_key_end - client_key_start;
#else
    config.uri = "ws://echo.websocket.events";
    config.transport = WEBSOCKET_TRANSPORT_OVER_TCP;
#endif

    hd_ws_client = esp_websocket_client_init(&config);
    esp_websocket_register_events(hd_ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    esp_websocket_client_start(hd_ws_client);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    wifi_event_sta_connected_t* conn_event = NULL;
    wifi_event_sta_disconnected_t* dis_event = NULL;
    ip_event_got_ip_t* got_event = NULL;

    if (WIFI_EVENT == event_base) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "wifi start connect, %s:%s", EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PWD);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            conn_event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "wifi connected, channel:%u authmode:%u", conn_event->channel, conn_event->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            dis_event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "wifi disconnected, reason:%u", dis_event->reason);
            break;
        default:
            ESP_LOGW(TAG, "unknown WIFI_EVENT:%ld", event_id);
            break;
        }
    }
    
    if (IP_EVENT == event_base) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            got_event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_event->ip_info.ip), IP2STR(&got_event->ip_info.netmask), IP2STR(&got_event->ip_info.gw));
            start_websocket_client();
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGE(TAG, "lost ip");
            break;
        default:
            ESP_LOGW(TAG, "unknown IP_EVENT:%ld", event_id);
            break;   
        }
    }
}

void app_main(void)
{
    esp_err_t err = ESP_OK;
    
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_flash_init error:%d", err);
        return;
    }

    esp_event_loop_create_default();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);

    esp_netif_init();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);

    wifi_config_t sta_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PWD,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_start();

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
