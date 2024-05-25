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

#define EXAMPLE_WIFI_SSID           "SolaxGuest"
#define EXAMPLE_WIFI_PWD            "solaxpower"
#define EXAMPLE_MAX_RECONNECT       5
#define EXAMPLE_UDP_SERVER_PORT     60001

static const char *TAG = "udp_server";
static uint32_t reconnect_cnt = 0;

static void udp_server_task(void *pvParameters)
{
    int sock = 0;
    int err = 0;
    int rx_len = 0;
    int tx_len = 0;
    uint8_t rx_buf[128] = { 0 };

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        return;
    }

    struct sockaddr_in ser_addr = { 0 };
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ser_addr.sin_port = htons(EXAMPLE_UDP_SERVER_PORT);
    err = bind(sock, (struct sockaddr *)&ser_addr, sizeof(ser_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket bind failed:%d", errno);
        return;
    }

    ESP_LOGI(TAG, "listen port:%u, waiting client data...", EXAMPLE_UDP_SERVER_PORT);
    while (1) {
        struct sockaddr_in client_addr = { 0 };
        socklen_t client_addr_len = sizeof(client_addr);
        rx_len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (rx_len < 0) {
            ESP_LOGE(TAG, "recv failed:%d", errno);
        } else {
            ESP_LOGI(TAG, "recv from %s:%d %s", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, rx_buf);
            tx_len = sendto(sock, rx_buf, rx_len, 0, (struct sockaddr *)&client_addr, client_addr_len);
            if (tx_len < 0) {
                ESP_LOGE(TAG, "send failed:%d", errno);
            }
        }
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
            ESP_LOGI(TAG, "start connect to %s", EXAMPLE_WIFI_SSID);
            esp_wifi_connect(); // non-block
            break;
        case WIFI_EVENT_STA_CONNECTED:
            conn_event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "connected, channel:%u, authmode:%u", conn_event->channel, conn_event->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            dis_event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "disconnected, reason:%u", dis_event->reason);
            if (reconnect_cnt < EXAMPLE_MAX_RECONNECT) {
                reconnect_cnt++;
                ESP_LOGW(TAG, "start reconnect %u", reconnect_cnt);
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "connect failed");
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
            ESP_LOGI(TAG, "get ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_event->ip_info.ip), IP2STR(&got_event->ip_info.netmask), IP2STR(&got_event->ip_info.gw));
            xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);
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
    esp_wifi_start();

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}