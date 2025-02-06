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
#include "mdns.h"


#define CONFIG_WIFI_SSID                    "SolaxGuest"
#define CONFIG_WIFI_PWD                     "solaxpower"
#define CONFIG_MDNS_INSNAME_1               "udp_echo_ins"
#define CONFIG_MDNS_SRVTYPE_1               "_echo_srv"
#define CONFIG_MDNS_TRANSPORT_1             "_udp"
#define CONFIG_MDNS_HOSTNAME_1              "abcd1234" // need endwith .local?
#define CONFIG_MDNS_PORT_1                  60001
#define CONFIG_MDNS_INSNAME_2               "tcp_echo_ins"
#define CONFIG_MDNS_SRVTYPE_2               "_echo_srv"
#define CONFIG_MDNS_TRANSPORT_2             "_tcp"
#define CONFIG_MDNS_HOSTNAME_2              "6789xyz" // need endwith .local?
#define CONFIG_MDNS_PORT_2                  60002


static const char *TAG = "mdns_server";

static void udp_echo_cb(void *pvParameters) {
    int sock = 0;
    int err = 0;
    int rx_len = 0;
    uint8_t rx_buf[128] = {0};
    struct sockaddr_in local_addr = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "udp socket create failed:%d", errno);
        goto exit;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(CONFIG_MDNS_PORT_1);
    err = bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "udp socket bind failed:%d", errno);
        goto exit;
    }

    ESP_LOGI(TAG, "udp server success, port:%u", CONFIG_MDNS_PORT_1);

    while (1) {
        rx_len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (rx_len > 0) {
            rx_buf[rx_len] = 0;
            ESP_LOGI(TAG, "UDP %s:%d >> %s", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, rx_buf);
            sendto(sock, rx_buf, rx_len, 0, (struct sockaddr *)&client_addr, client_addr_len);
            ESP_LOGI(TAG, "UDP %s:%d << %s", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, rx_buf);
        }
    }

exit:
    vTaskDelete(NULL);
}

static void tcp_echo_cb(void *pvParameters) {
    int listen_sock = 0;
    int client_sock = 0;
    int err = 0;
    int rx_len = 0;
    uint8_t rx_buf[128] = { 0 };
    struct sockaddr_in local_addr = { 0 };
    struct sockaddr_in client_addr = { 0 };
    socklen_t client_addr_len = sizeof(client_addr);

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "tcp socket create failed:%d", errno);
        goto exit;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(CONFIG_MDNS_PORT_2);
    err = bind(listen_sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "tcp socket bind failed:%d", errno);
        goto exit;
    }

    listen(listen_sock, 1);
    ESP_LOGI(TAG, "tcp server success, listen:%u", CONFIG_MDNS_PORT_2);

    client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock < 0) {
        ESP_LOGE(TAG, "tcp socket accept failed:%d", errno);
        goto exit;
    }
    ESP_LOGI(TAG, "tcp client connected, %s:%u", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);

    while (1) {
        rx_len = recv(client_sock, rx_buf, sizeof(rx_buf), 0);
        if (rx_len < 0) {
            ESP_LOGE(TAG, "tcp socket recv failed:%d", errno);
            close(client_sock);
            goto exit;
        } else if (rx_len == 0) {
            ESP_LOGW(TAG, "tcp socket closed");
            close(client_sock);
            goto exit;
        } else {
            rx_buf[rx_len] = 0;
            ESP_LOGI(TAG, "TCP %s:%d >> %s", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, rx_buf);
            send(client_sock, rx_buf, rx_len, 0);
            ESP_LOGI(TAG, "TCP %s:%d << %s", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, rx_buf);
        }
    }

exit:
    vTaskDelete(NULL);
}

static void mdns_server_cb(void *pvParameters) {
    mdns_txt_item_t txt_items_1[] = {
        {"board", "ESP32"},
        {"id", "udp echo service"},
    };
    mdns_txt_item_t txt_items_2[] = {
        {"board", "ESP32"},
        {"id", "tcp echo service"},
    };

    mdns_init();
    mdns_service_add_for_host(CONFIG_MDNS_INSNAME_1, CONFIG_MDNS_SRVTYPE_1, CONFIG_MDNS_TRANSPORT_1,
        CONFIG_MDNS_HOSTNAME_1, CONFIG_MDNS_PORT_1, txt_items_1, sizeof(txt_items_1)/sizeof(txt_items_1[0]));
    mdns_service_add_for_host(CONFIG_MDNS_INSNAME_2, CONFIG_MDNS_SRVTYPE_2, CONFIG_MDNS_TRANSPORT_2,
        CONFIG_MDNS_HOSTNAME_2, CONFIG_MDNS_PORT_2, txt_items_2, sizeof(txt_items_2)/sizeof(txt_items_2[0]));

    xTaskCreate(udp_echo_cb, "udp_echo", 4096, NULL, 3, NULL);
    xTaskCreate(tcp_echo_cb, "tcp_echo", 4096, NULL, 3, NULL);

    vTaskDelete(NULL);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    wifi_event_sta_connected_t* evt_sta_conn = NULL;
    wifi_event_sta_disconnected_t* evt_sta_dis = NULL;
    ip_event_got_ip_t* evt_got_ip = NULL;

    if (WIFI_EVENT == event_base) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            ESP_LOGI(TAG, "start connect, ssid:%s", CONFIG_WIFI_SSID);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            evt_sta_conn = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED, channel:%u, authmode:%u", evt_sta_conn->channel, evt_sta_conn->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            evt_sta_dis = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "WIFI_EVENT_STA_DISCONNECTED, reason:%u", evt_sta_dis->reason);
            break;
        default:
            ESP_LOGW(TAG, "unknown WIFI_EVENT:%ld", event_id);
            break;
        }
    }

    if (IP_EVENT == event_base) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            evt_got_ip = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&evt_got_ip->ip_info.ip), IP2STR(&evt_got_ip->ip_info.netmask), IP2STR(&evt_got_ip->ip_info.gw));
            xTaskCreate(mdns_server_cb, "mdns_server", 4096, NULL, 3, NULL);
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGE(TAG, "IP_EVENT_STA_LOST_IP");
            break;
        default:
            ESP_LOGW(TAG, "unknown IP_EVENT:%ld", event_id);
            break;   
        }
    }
}

void app_main(void) {
    esp_err_t err = ESP_OK;
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PWD,
        },
    };

    err = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == err || ESP_ERR_NVS_NEW_VERSION_FOUND == err) {
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

    esp_wifi_init(&init_cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
