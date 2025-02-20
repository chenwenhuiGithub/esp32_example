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


#define CONFIG_WIFI_SSID                    "wenhui"
#define CONFIG_WIFI_PWD                     "12345678"
#define CONFIG_MDNS_SRVTYPE                 "_echosrv"
#define CONFIG_MDNS_TRANSPORT               "_udp"


static const char *TAG = "mdns_client";

static void mdns_query_cb(void *pvParameters) {
    esp_err_t err = ESP_OK;
    mdns_result_ptr_t ret_ptr = {0};
    mdns_result_srv_t ret_srv = {0};
    mdns_result_txt_t ret_txt[5] = {0};
    uint32_t txt_cnt = 0;

    mdns_init();

    ESP_LOGI(TAG, "send query PTR, %s.%s.local", CONFIG_MDNS_SRVTYPE, CONFIG_MDNS_TRANSPORT);
    err = mdns_query_ptr(CONFIG_MDNS_SRVTYPE, CONFIG_MDNS_TRANSPORT, &ret_ptr);
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "recv resp ok, instance:%s", ret_ptr.instance);
    } else {
        ESP_LOGE(TAG, "recv resp err:%d", err);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "send query SRV, %s.%s.%s.local", ret_ptr.instance, CONFIG_MDNS_SRVTYPE, CONFIG_MDNS_TRANSPORT);
    err = mdns_query_srv(ret_ptr.instance, CONFIG_MDNS_SRVTYPE, CONFIG_MDNS_TRANSPORT, &ret_srv);
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "recv resp ok, priority:%u, weight:%u, port:%u, target:%s",
            ret_srv.priority, ret_srv.weight, ret_srv.port, ret_srv.target);
    } else {
        ESP_LOGE(TAG, "recv resp err:%d", err);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "send query TXT, %s.%s.%s.local", ret_ptr.instance, CONFIG_MDNS_SRVTYPE, CONFIG_MDNS_TRANSPORT);
    err = mdns_query_txt(ret_ptr.instance, CONFIG_MDNS_SRVTYPE, CONFIG_MDNS_TRANSPORT, ret_txt, &txt_cnt);
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "recv resp ok, txt_cnt:%lu", txt_cnt);
        for (uint32_t i = 0; i < txt_cnt; i++) {
            ESP_LOGI(TAG, "%s:%s", ret_txt[i].key, ret_txt[i].value);
        }
    } else {
        ESP_LOGE(TAG, "recv resp err:%d", err);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

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

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
