#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netcfg.h"
#include "cloud.h"
#include "ir.h"


#define CONFIG_WIFI_AP_IP                               "192.168.10.10"
#define CONFIG_WIFI_AP_NETMASK                          "255.255.255.0"
#define CONFIG_WIFI_AP_CHANNEL                          5
#define CONFIG_WIFI_AP_MAX_CONN                         2


static const char *TAG = "main";

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    wifi_event_sta_connected_t *sta_conn_event = NULL;
    wifi_event_sta_disconnected_t *sta_disconn_event = NULL;
    wifi_event_ap_staconnected_t *ap_sta_conn_event = NULL;
    wifi_event_ap_stadisconnected_t *ap_sta_disconn_event = NULL;
    ip_event_got_ip_t *got_ip_event = NULL;

    if (WIFI_EVENT == event_base) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            sta_conn_event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED, channel:%u authmode:%u aid:%u bssid:%02X:%02X:%02X:%02X:%02X:%02X",
                    sta_conn_event->channel, sta_conn_event->authmode, sta_conn_event->aid,
                    sta_conn_event->bssid[0], sta_conn_event->bssid[1], sta_conn_event->bssid[2],
                    sta_conn_event->bssid[3], sta_conn_event->bssid[4], sta_conn_event->bssid[5]);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            sta_disconn_event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "WIFI_EVENT_STA_DISCONNECTED, reason:%u rssi:%d bssid:%02X:%02X:%02X:%02X:%02X:%02X",
                    sta_disconn_event->reason, sta_disconn_event->rssi,
                    sta_disconn_event->bssid[0], sta_disconn_event->bssid[1], sta_disconn_event->bssid[2],
                    sta_disconn_event->bssid[3], sta_disconn_event->bssid[4], sta_disconn_event->bssid[5]);
            netcfg_set_netstat(NETSTAT_WIFI_NOT_CONNECTED);
            cloud_stop_connect();
            esp_wifi_connect();
            ESP_LOGI(TAG, "wifi start connect");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ap_sta_conn_event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED, aid:%u mac:%02X:%02X:%02X:%02X:%02X:%02X",
                    ap_sta_conn_event->aid,
                    ap_sta_conn_event->mac[0], ap_sta_conn_event->mac[1], ap_sta_conn_event->mac[2],
                    ap_sta_conn_event->mac[3], ap_sta_conn_event->mac[4], ap_sta_conn_event->mac[5]);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ap_sta_disconn_event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED, aid:%u reason:%u mac:%02X:%02X:%02X:%02X:%02X:%02X",
                    ap_sta_disconn_event->aid, ap_sta_disconn_event->reason,
                    ap_sta_disconn_event->mac[0], ap_sta_disconn_event->mac[1], ap_sta_disconn_event->mac[2],
                    ap_sta_disconn_event->mac[3], ap_sta_disconn_event->mac[4], ap_sta_disconn_event->mac[5]);
            break;
        default:
            ESP_LOGW(TAG, "unknown WIFI_EVENT:%ld", event_id);
            break;
        }
    }

    if (IP_EVENT == event_base) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            got_ip_event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP, ip:"IPSTR" netmask:"IPSTR" gw:"IPSTR,
                    IP2STR(&got_ip_event->ip_info.ip), IP2STR(&got_ip_event->ip_info.netmask), IP2STR(&got_ip_event->ip_info.gw));
            netcfg_set_netstat(NETSTAT_WIFI_CONNECTED);
            cloud_start_connect();
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGE(TAG, "IP_EVENT_STA_LOST_IP");
            netcfg_set_netstat(NETSTAT_WIFI_NOT_CONNECTED);
            cloud_stop_connect();
            esp_wifi_disconnect();
            esp_wifi_connect();
            ESP_LOGI(TAG, "wifi start connect");
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
    wifi_config_t sta_cfg = {0};
    wifi_config_t ap_cfg = {0};
    esp_netif_t *ap_netif = NULL;
    esp_netif_ip_info_t ap_netif_ip = {0};
    uint8_t sta_mac[6] = {0};
    // char stack_info[1024] = {0};

    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);

    esp_netif_init();
    esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    err = esp_wifi_init(&init_cfg);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "wifi init failed:%d", err);
        return;
    }
    ESP_LOGI(TAG, "wifi init ok");

    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
    sprintf((char *)ap_cfg.ap.ssid, "ESP32_%02X%02X", sta_mac[4], sta_mac[5]);
    ap_cfg.ap.ssid_len = strlen((char *)ap_cfg.ap.ssid);
    ap_cfg.ap.channel = CONFIG_WIFI_AP_CHANNEL;
    ap_cfg.ap.max_connection = CONFIG_WIFI_AP_MAX_CONN;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    ap_cfg.ap.ssid_hidden = 0;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_cfg);
    err = esp_wifi_start();
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "wifi start failed:%d", err);
        return;
    }
    ESP_LOGI(TAG, "wifi start ok, ap_ssid:%s", ap_cfg.ap.ssid);

    esp_netif_dhcps_stop(ap_netif);
    ap_netif_ip.ip.addr = esp_ip4addr_aton(CONFIG_WIFI_AP_IP);
    ap_netif_ip.netmask.addr = esp_ip4addr_aton(CONFIG_WIFI_AP_NETMASK);
    ap_netif_ip.gw.addr = esp_ip4addr_aton(CONFIG_WIFI_AP_IP);
    esp_netif_set_ip_info(ap_netif, &ap_netif_ip);
    err = esp_netif_dhcps_start(ap_netif);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "wifi dhcps start failed:%d", err);
        return;
    }
    ESP_LOGI(TAG, "wifi dhcps start ok");

    netcfg_get_wifi_info((char *)sta_cfg.sta.ssid, (char *)sta_cfg.sta.password);
    if (sta_cfg.sta.ssid[0]) {
        ESP_LOGI(TAG, "got wifi netcfg info, %s:%s", sta_cfg.sta.ssid, sta_cfg.sta.password);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        esp_wifi_connect();
        ESP_LOGI(TAG, "wifi start connect");
    } else {
        ESP_LOGW(TAG, "wifi not netcfg yet");
    }

    netcfg_init();

    ir_init();

    while (1) {
        ESP_LOGI(TAG, "heap size, cur_free:%u min_free:%u largest_free_block:%u",
            heap_caps_get_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL),
            heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL),
            heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));
        // vTaskList(stack_info);
        // ESP_LOGI(TAG, "stack size,\n%s", stack_info);
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}
