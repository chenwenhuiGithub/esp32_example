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
#define EXAMPLE_TCP_SERVER_PORT         60001
#define EXAMPLE_TCP_SERVER_MAX_CLIENT   5

static const char *TAG = "tcp_select";
static uint32_t wifi_reconnect_cnt = 0;

static void tcp_select_task(void *pvParameters)
{
    int listen_sock = 0;
    int client_sock = 0;
    int err = 0;
    int rx_len = 0;
    uint8_t rx_buf[256] = { 0 };
    struct sockaddr_in local_addr = { 0 };
    struct sockaddr_in client_addr = { 0 };
    socklen_t client_addr_len = sizeof(client_addr);
    uint8_t i = 0;
    int flags = 0;
    int client_sockets[EXAMPLE_TCP_SERVER_MAX_CLIENT] = { 0 };
    int max_fd = 0;
    fd_set readfds;

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        goto exit;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(EXAMPLE_TCP_SERVER_PORT);
    err = bind(listen_sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket bind failed:%d", errno);
        goto exit;
    }

    listen(listen_sock, 1);
    ESP_LOGI(TAG, "tcp server listen, port:%u", EXAMPLE_TCP_SERVER_PORT);

    flags = fcntl(listen_sock, F_GETFL);
    err = fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK);
    if (err < 0) {
        ESP_LOGE(TAG, "socket fcntl failed:%d", errno);
        goto exit;
    }

    for (i = 0; i < EXAMPLE_TCP_SERVER_MAX_CLIENT; i++) {
        client_sockets[i] = -1;
    }
    max_fd = listen_sock;

    struct timeval tv = { 0 };
    tv.tv_sec = 3;
    tv.tv_usec = 0;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(listen_sock, &readfds);
        for (i = 0; i < EXAMPLE_TCP_SERVER_MAX_CLIENT; i++) {
            if (client_sockets[i] != -1) {
                FD_SET(client_sockets[i], &readfds);
                if (client_sockets[i] > max_fd) {
                    max_fd = client_sockets[i];
                }
            }
        }

        int cnt = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (cnt < 0) {
            ESP_LOGE(TAG, "socket select failed:%d", errno);
            goto exit; 
        } else if (0 == cnt) {
            ESP_LOGW(TAG, "select timeout");
            continue;
        } else {
            if (FD_ISSET(listen_sock, &readfds)) {
                client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
                if (client_sock < 0) {
                    ESP_LOGE(TAG, "socket accept failed:%d", errno);
                } else {
                    ESP_LOGI(TAG, "tcp client connected, %s:%u", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
                    for (i = 0; i < EXAMPLE_TCP_SERVER_MAX_CLIENT; i++) {
                        if (client_sockets[i] == -1) {
                            client_sockets[i] = client_sock;
                            break;
                        }
                    }
                    if (i == EXAMPLE_TCP_SERVER_MAX_CLIENT) {
                        ESP_LOGE(TAG, "max clients connected");
                        close(client_sock);
                        continue;
                    }

                    flags = fcntl(client_sock, F_GETFL);
                    err = fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);
                    if (err < 0) {
                        ESP_LOGE(TAG, "socket fcntl failed:%d", errno);
                        goto exit;
                    }
                }

            }

            for (i = 0; i < EXAMPLE_TCP_SERVER_MAX_CLIENT; i++) {
                if (FD_ISSET(client_sockets[i], &readfds)) {
                    rx_len = recv(client_sockets[i], rx_buf, sizeof(rx_buf), 0);
                    if (rx_len < 0) {
                        ESP_LOGE(TAG, "socket recv failed:%d", errno);
                        close(client_sockets[i]);
                        client_sockets[i] = -1;
                    } else if (rx_len == 0) {
                        ESP_LOGW(TAG, "socket client closed");
                        close(client_sockets[i]);
                        client_sockets[i] = -1;
                    } else {
                        rx_buf[rx_len] = 0;
                        ESP_LOGI(TAG, "recv:%s", rx_buf);
                        send(client_sockets[i], rx_buf, rx_len, 0);
                    }                      
                }
            }
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
            xTaskCreate(tcp_select_task, "tcp_select", 4096, NULL, 5, NULL);
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
