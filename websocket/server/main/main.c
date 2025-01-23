#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_https_server.h"


#define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_USE_WSS                             0

#if EXAMPLE_USE_WSS == 1
extern const uint8_t server_crt_start[]             asm("_binary_server_crt_start");
extern const uint8_t server_crt_end[]               asm("_binary_server_crt_end");
extern const uint8_t server_priv_key_start[]        asm("_binary_server_priv_key_start");
extern const uint8_t server_priv_key_end[]          asm("_binary_server_priv_key_end");
#endif

static esp_err_t echo_get_handler(httpd_req_t *req);

static const char *TAG = "websocket_server";

static const httpd_uri_t uri_ws = {
    .uri       = "/echo",
    .method    = HTTP_GET,
    .handler   = echo_get_handler,
    .user_ctx  = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = true
};

static esp_err_t echo_get_handler(httpd_req_t *req)
{
    httpd_ws_frame_t ws_pkt = {0};
    uint8_t *buf = NULL;
    esp_err_t ret = ESP_OK;

    if (HTTP_GET == req->method) {
        ESP_LOGI(TAG, "Handshake done");
        return ESP_OK;        
    }
    
    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed, ret:%d", ret);
        return ret;
    }

    buf = malloc(ws_pkt.len + 1);
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed, ret:%d", ret);
        free(buf);
        return ret;
    }

    ESP_LOGI(TAG, "recv ws frame, final:%d type(0-subpkg,1-text,2-hex,8-disconn,9-ping,10-pong):%d len:%d data:",
        ws_pkt.final, ws_pkt.type, ws_pkt.len);
    if (HTTPD_WS_TYPE_TEXT == ws_pkt.type) {
        ws_pkt.payload[ws_pkt.len] = 0;
        ESP_LOGI(TAG, "%s", ws_pkt.payload);
    } else if (HTTPD_WS_TYPE_BINARY == ws_pkt.type) {
        ESP_LOG_BUFFER_HEX(TAG, ws_pkt.payload, ws_pkt.len);
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed, ret:%d", ret);
    }
    free(buf);

    return ESP_OK;
}

static void start_websocket_server()
{
    httpd_handle_t hd_server = NULL;

#if EXAMPLE_USE_WSS == 1
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = server_crt_start;
    config.servercert_len = server_crt_end - server_crt_start;
    config.prvtkey_pem = server_priv_key_start;
    config.prvtkey_len = server_priv_key_end - server_priv_key_start;
    if (httpd_ssl_start(&hd_server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "start https server ok, port:%d", config.port_secure);
#else
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&hd_server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "start http server ok, port:%d", config.server_port);
#endif
        httpd_register_uri_handler(hd_server, &uri_ws);
    } else {
        ESP_LOGE(TAG, "start http server failed");        
    }
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
            start_websocket_server();
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
