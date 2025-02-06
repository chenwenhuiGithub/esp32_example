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
#define CONFIG_MDNS_SRVTYPE_1               "_echo_srv"
#define CONFIG_MDNS_TRANSPORT_1             "_udp"
#define CONFIG_MDNS_SRVTYPE_2               "_echo_srv"
#define CONFIG_MDNS_TRANSPORT_2             "_tcp"


static const char *TAG = "mdns_server";

static void udp_echo_test() {
    esp_err_t err = ESP_OK;
    mdns_result_t *ptr_results = NULL;
    mdns_result_t *srv_results = NULL;
    mdns_result_t *txt_results = NULL;
    mdns_result_t *result = NULL;
    char insname[64] = {0};
    char hostname[64] = {0};
    uint16_t port = 0;
    struct esp_ip4_addr ip_addr = {0};
    uint32_t i = 0;
    int sock = 0;
    uint8_t rx_buf[128] = {0};
    struct sockaddr_in server_addr = {0};
    socklen_t server_addr_len = sizeof(server_addr);

    ESP_LOGI(TAG, "query PTR: %s.%s.local", CONFIG_MDNS_SRVTYPE_1, CONFIG_MDNS_TRANSPORT_1);
    err = mdns_query_ptr(CONFIG_MDNS_SRVTYPE_1, CONFIG_MDNS_TRANSPORT_1, 3000, 3,  &ptr_results);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "query failed:%d", err);
    } else if (!ptr_results) {
        ESP_LOGW(TAG, "not found");
    } else {
        result = ptr_results;
        while (result) {
            ESP_LOGI(TAG, "%s.%s.%s.local", result->instance_name, result->service_type, result->proto);
            result = result->next;
        }

        result = ptr_results; // save first SRV instance_name
        memcpy(insname, result->instance_name, strlen(result->instance_name));
    }

    ESP_LOGI(TAG, "query SRV: %s.%s.%s.local", insname, CONFIG_MDNS_SRVTYPE_1, CONFIG_MDNS_TRANSPORT_1);
    err = mdns_query_srv(insname, CONFIG_MDNS_SRVTYPE_1, CONFIG_MDNS_TRANSPORT_1, 3000, &srv_results);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "query failed:%d", err);
    } else if (!srv_results) {
        ESP_LOGW(TAG, "no found");
    } else {
        result = srv_results;
        ESP_LOGI(TAG, "port:%u hostname:%s", result->port, result->hostname);

        memcpy(hostname, result->hostname, strlen(result->hostname));
        port = result->port;
    }

    ESP_LOGI(TAG, "query TXT: %s.%s.%s.local", insname, CONFIG_MDNS_SRVTYPE_1, CONFIG_MDNS_TRANSPORT_1);
    err = mdns_query_txt(insname, CONFIG_MDNS_SRVTYPE_1, CONFIG_MDNS_TRANSPORT_1, 3000, &txt_results);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "query failed:%d", err);
    } else if (!txt_results) {
        ESP_LOGW(TAG, "no found");
    } else {
        result = txt_results;
        for (i = 0; i < result->txt_count; i++) {
            ESP_LOGI(TAG, "%s:%s", result->txt[i].key, result->txt[i].value);
        }
    } 

    ESP_LOGI(TAG, "query A: %s", hostname);
    err = mdns_query_a(hostname, 3000, &ip_addr);
    if (ESP_OK != err) {
        if (ESP_ERR_NOT_FOUND == err) {
            ESP_LOGW(TAG, "no found");
        } else {
            ESP_LOGE(TAG, "query failed:%d", err);
        }
    } else {
        ESP_LOGI(TAG, IPSTR, IP2STR(&ip_addr));
    }

    ESP_LOGI(TAG, "start udp socket test, "IPSTR":%u", IP2STR(&ip_addr), port);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "udp socket create failed:%d", errno);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = ip_addr.addr;
    server_addr.sin_port = htons(port);

    sendto(sock, "hello world", strlen("hello world"), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ESP_LOGI(TAG, "UDP %s:%d << %s", inet_ntoa(server_addr.sin_addr), server_addr.sin_port, "hello world");
    recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&server_addr, &server_addr_len);
    ESP_LOGI(TAG, "UDP %s:%d >> %s", inet_ntoa(server_addr.sin_addr), server_addr.sin_port, rx_buf);

    mdns_query_results_free(ptr_results);
    mdns_query_results_free(srv_results);
    mdns_query_results_free(txt_results);
}

static void tcp_echo_test() {
    esp_err_t err = ESP_OK;
    mdns_result_t *ptr_results = NULL;
    mdns_result_t *srv_results = NULL;
    mdns_result_t *txt_results = NULL;
    mdns_result_t *result = NULL;
    char insname[64] = {0};
    char hostname[64] = {0};
    uint16_t port = 0;
    struct esp_ip4_addr ip_addr = {0};
    uint32_t i = 0;
    int sock = 0;
    uint8_t rx_buf[128] = {0};
    struct sockaddr_in server_addr = {0};

    ESP_LOGI(TAG, "query PTR: %s.%s.local", CONFIG_MDNS_SRVTYPE_2, CONFIG_MDNS_TRANSPORT_2);
    err = mdns_query_ptr(CONFIG_MDNS_SRVTYPE_2, CONFIG_MDNS_TRANSPORT_2, 3000, 3,  &ptr_results);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "query failed:%d", err);
    } else if (!ptr_results) {
        ESP_LOGW(TAG, "not found");
    } else {
        result = ptr_results;
        while (result) {
            ESP_LOGI(TAG, "%s.%s.%s.local", result->instance_name, result->service_type, result->proto);
            result = result->next;
        }

        result = ptr_results; // save first SRV instance_name
        memcpy(insname, result->instance_name, strlen(result->instance_name));
    }

    ESP_LOGI(TAG, "query SRV: %s.%s.%s.local", insname, CONFIG_MDNS_SRVTYPE_2, CONFIG_MDNS_TRANSPORT_2);
    err = mdns_query_srv(insname, CONFIG_MDNS_SRVTYPE_2, CONFIG_MDNS_TRANSPORT_2, 3000, &srv_results);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "query failed:%d", err);
    } else if (!srv_results) {
        ESP_LOGW(TAG, "no found");
    } else {
        result = srv_results;
        ESP_LOGI(TAG, "port:%u hostname:%s", result->port, result->hostname);

        memcpy(hostname, result->hostname, strlen(result->hostname));
        port = result->port;
    }

    ESP_LOGI(TAG, "query TXT: %s.%s.%s.local", insname, CONFIG_MDNS_SRVTYPE_2, CONFIG_MDNS_TRANSPORT_2);
    err = mdns_query_txt(insname, CONFIG_MDNS_SRVTYPE_2, CONFIG_MDNS_TRANSPORT_2, 3000, &txt_results);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "query failed:%d", err);
    } else if (!txt_results) {
        ESP_LOGW(TAG, "no found");
    } else {
        result = txt_results;
        for (i = 0; i < result->txt_count; i++) {
            ESP_LOGI(TAG, "%s:%s", result->txt[i].key, result->txt[i].value);
        }
    } 

    ESP_LOGI(TAG, "query A: %s", hostname);
    err = mdns_query_a(hostname, 3000, &ip_addr);
    if (ESP_OK != err) {
        if (ESP_ERR_NOT_FOUND == err) {
            ESP_LOGW(TAG, "no found");
        } else {
            ESP_LOGE(TAG, "query failed:%d", err);
        }
    } else {
        ESP_LOGI(TAG, IPSTR, IP2STR(&ip_addr));
    }

    ESP_LOGI(TAG, "start tcp socket test, "IPSTR":%u", IP2STR(&ip_addr), port);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "tcp socket create failed:%d", errno);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = ip_addr.addr;
    server_addr.sin_port = htons(port);
    err = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "tcp socket connect failed:%d", errno);
        return;
    }
    ESP_LOGI(TAG, "tcp socket connect success");

    send(sock, "hello world", strlen("hello world"), 0);
    ESP_LOGI(TAG, "TCP %s:%d << %s", inet_ntoa(server_addr.sin_addr), server_addr.sin_port, "hello world");
    recv(sock, rx_buf, sizeof(rx_buf), 0);
    ESP_LOGI(TAG, "TCP %s:%d >> %s", inet_ntoa(server_addr.sin_addr), server_addr.sin_port, rx_buf);

    mdns_query_results_free(ptr_results);
    mdns_query_results_free(srv_results);
    mdns_query_results_free(txt_results);
}

static void mdns_query_cb(void *pvParameters) {
    udp_echo_test();
    tcp_echo_test();

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
            xTaskCreate(mdns_query_cb, "mdns_query", 4096, NULL, 3, NULL);
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

    mdns_init();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
