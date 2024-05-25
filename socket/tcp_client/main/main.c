#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define EXAMPLE_WIFI_SSID               "TP-LINK_8E86"
#define EXAMPLE_WIFI_PWD                "12345678"
#define EXAMPLE_WIFI_MAX_RECONNECT      3
#define EXAMPLE_TCP_SERVER_IP           "192.168.0.102"
#define EXAMPLE_TCP_SERVER_PORT         60001
#define EXAMPLE_TCP_LOCAL_PORT          60002

static const char *TAG = "tcp_client";
static uint32_t wifi_reconnect_cnt = 0;

static void tcp_client_task(void *pvParameters)
{
    int sock = 0;
    int err = 0;
    int rx_len = 0;
    uint8_t rx_buf[256] = { 0 };
    struct sockaddr_in local_addr = { 0 };
    struct sockaddr_in server_addr = { 0 };

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        goto exit;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(EXAMPLE_TCP_LOCAL_PORT);
    err = bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket bind failed:%d", errno);
        goto exit;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(EXAMPLE_TCP_SERVER_IP);
    server_addr.sin_port = htons(EXAMPLE_TCP_SERVER_PORT);
    ESP_LOGI(TAG, "tcp client start connect %s:%u", EXAMPLE_TCP_SERVER_IP, EXAMPLE_TCP_SERVER_PORT);
    err = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket connect failed:%d", errno);
    } else {
        ESP_LOGI(TAG, "socket connect success");
    }

    while (1) {
        err = send(sock, "hello world 123", strlen("hello world 123"), 0);
        if (err < 0) {
            ESP_LOGE(TAG, "socket send failed:%d", errno);
            break;
        }

        rx_len = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
        if (rx_len < 0) {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
            break;
        } else {
            rx_buf[rx_len] = 0;
            ESP_LOGI(TAG, "recv:%s", rx_buf);
        }
    }

exit:
    vTaskDelete(NULL);
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
            esp_wifi_connect(); // non-block
            break;
        case WIFI_EVENT_STA_CONNECTED:
            conn_event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "wifi connected, channel:%u authmode:%u", conn_event->channel, conn_event->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            dis_event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "wifi disconnected, reason:%u", dis_event->reason);
            if (wifi_reconnect_cnt < EXAMPLE_WIFI_MAX_RECONNECT) {
                wifi_reconnect_cnt++;
                ESP_LOGW(TAG, "wifi start reconnect, cnt:%u", wifi_reconnect_cnt);
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "wifi connect failed");
            }
            break;
        default:
            ESP_LOGW(TAG, "unknown WIFI_EVENT:%d", event_id);
            break;
        }
    }
    
    if (IP_EVENT == event_base) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            got_event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_event->ip_info.ip), IP2STR(&got_event->ip_info.netmask), IP2STR(&got_event->ip_info.gw));
            xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGE(TAG, "lost ip");
            break;
        default:
            ESP_LOGW(TAG, "unknown IP_EVENT:%d", event_id);
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
    esp_wifi_start(); // trigger WIFI_EVENT_STA_START

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
