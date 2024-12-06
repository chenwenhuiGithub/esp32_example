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


#define EXAMPLE_WIFI_SSID               "SolaxGuest"
#define EXAMPLE_WIFI_PWD                "solaxpower"
#define EXAMPLE_MDNS_INSTANCE           "echo_ins"
#define EXAMPLE_MDNS_SERVICE            "_conn_srv"
#define EXAMPLE_MDNS_TRANSPORT          "_udp"
#define EXAMPLE_MDNS_HOSTNAME           "abcd1234xyz"
#define EXAMPLE_MDNS_PORT               60001


static const char *TAG = "mdns";

static void udp_task(void *pvParameters)
{
    int sock = 0;
    int err = 0;
    int rx_len = 0;
    uint8_t rx_buf[256] = { 0 };
    struct sockaddr_in local_addr = { 0 };
    struct sockaddr_in client_addr = { 0 };
    socklen_t client_addr_len = sizeof(client_addr);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        goto exit;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(EXAMPLE_MDNS_PORT);
    err = bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket bind failed:%d", errno);
        goto exit;
    }

    ESP_LOGI(TAG, "udp server ok, port:%u", EXAMPLE_MDNS_PORT);
    while (1) {
        rx_len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (rx_len > 0) {
            ESP_LOGI(TAG, "%s:%d >> %s", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, rx_buf);
            sendto(sock, rx_buf, rx_len, 0, (struct sockaddr *)&client_addr, client_addr_len);
            ESP_LOGI(TAG, "%s:%d << %s", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, rx_buf);
        }
    }

exit:
    vTaskDelete(NULL);
}

static void mdns_task(void *pvParameters) {
    esp_err_t err = ESP_OK;
    char *g_hostname = "ESP32_hostname";
    char *g_instancename = "ESP32_instancename";
    mdns_txt_item_t txt_items[] = {
        {"board", "ESP32"},
        {"user", "admin"},
        {"pwd", "123456"}
    };
    mdns_result_t *results = NULL;
    mdns_result_t *result = NULL;
    struct esp_ip4_addr ip_addr = {0};
    uint32_t i = 0;

    mdns_init();
    mdns_hostname_set(g_hostname);
    mdns_instance_name_set(g_instancename);

    mdns_service_add_for_host(EXAMPLE_MDNS_INSTANCE, EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT, EXAMPLE_MDNS_HOSTNAME,
        EXAMPLE_MDNS_PORT, txt_items, sizeof(txt_items)/sizeof(txt_items[0]));

    while (1) {
        ESP_LOGI(TAG, "query PTR: %s.%s.local", EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT);
        err = mdns_query_ptr(EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT, 5000, 3,  &results);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "query failed: %s", esp_err_to_name(err));
        } else if (!results) {
            ESP_LOGW(TAG, "no found");
        } else {
            result = results;
            while (result) {
                ESP_LOGI(TAG, "answer: %s.%s.%s.local", result->instance_name, result->service_type, result->proto);
                result = result->next;
            }
            mdns_query_results_free(results);
        }
        
        ESP_LOGI(TAG, "query SRV: %s.%s.%s.local", EXAMPLE_MDNS_INSTANCE, EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT);
        err = mdns_query_srv(EXAMPLE_MDNS_INSTANCE, EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT, 5000, &results);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "query failed: %s", esp_err_to_name(err));
        } else if (!results) {
            ESP_LOGW(TAG, "no found");
        } else {
            result = results;
            ESP_LOGI(TAG, "answer: %s.%s.%s.local port:%u hostname:%s", EXAMPLE_MDNS_INSTANCE, EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT,
                result->port, result->hostname);
            mdns_query_results_free(results);
        }

        ESP_LOGI(TAG, "query TXT: %s.%s.%s.local", EXAMPLE_MDNS_INSTANCE, EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT);
        err = mdns_query_txt(EXAMPLE_MDNS_INSTANCE, EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT, 5000, &results);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "query failed: %s", esp_err_to_name(err));
        } else if (!results) {
            ESP_LOGW(TAG, "no found");
        } else {
            result = results;
            ESP_LOGI(TAG, "answer: %s.%s.%s.local cnt:%u", EXAMPLE_MDNS_INSTANCE, EXAMPLE_MDNS_SERVICE, EXAMPLE_MDNS_TRANSPORT, result->txt_count);
            if (result->txt_count) {
                for (i = 0; i < result->txt_count; i++) {
                    ESP_LOGI(TAG, "%s=%s", result->txt[i].key, result->txt[i].value);
                }
            }
            mdns_query_results_free(results);
        } 

        ESP_LOGI(TAG, "query A: %s.local", EXAMPLE_MDNS_HOSTNAME);
        err = mdns_query_a(EXAMPLE_MDNS_HOSTNAME, 5000, &ip_addr);
        if (ESP_OK != err) {
            if (ESP_ERR_NOT_FOUND == err) {
                ESP_LOGW(TAG, "no found");
            } else {
                ESP_LOGE(TAG, "query failed: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGI(TAG, "answer:" IPSTR, IP2STR(&ip_addr));
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
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            conn_event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "connected, channel:%u, authmode:%u", conn_event->channel, conn_event->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            dis_event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "disconnected, reason:%u", dis_event->reason);
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
            ESP_LOGI(TAG, "get ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_event->ip_info.ip), IP2STR(&got_event->ip_info.netmask), IP2STR(&got_event->ip_info.gw));
            xTaskCreate(udp_task, "udp_task", 4096, NULL, 3, NULL);
            xTaskCreate(mdns_task, "mdns_task", 4096, NULL, 5, NULL);
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
