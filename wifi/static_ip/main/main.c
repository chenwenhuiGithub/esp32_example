#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EXAMPLE_WIFI_SSID           "SolaxGuest"
#define EXAMPLE_WIFI_PWD            "solaxpower"
#define EXAMPLE_MAX_RECONNECT       5
#define EXAMPLE_STATIC_IP           "192.168.0.123"
#define EXAMPLE_STATIC_NETMASK      "255.255.255.0"
#define EXAMPLE_STATIC_GW           "192.168.0.1"
#define EXAMPLE_MAIN_DNS            "120.27.213.135"
#define EXAMPLE_BACKUP_DNS          "180.76.76.76"


static const char *TAG = "wifi_static_ip";
static uint32_t reconnect_cnt = 0;


static void set_static_ip(esp_netif_t *netif)
{
    if (esp_netif_dhcpc_stop(netif) != ESP_OK) {
        ESP_LOGE(TAG, "stop dhcp client failed");
        return;
    }

    esp_netif_ip_info_t ip = { 0 };
    ip.ip.addr = ipaddr_addr(EXAMPLE_STATIC_IP);
    ip.netmask.addr = ipaddr_addr(EXAMPLE_STATIC_NETMASK);
    ip.gw.addr = ipaddr_addr(EXAMPLE_STATIC_GW);
    if (esp_netif_set_ip_info(netif, &ip) != ESP_OK) {
        ESP_LOGE(TAG, "set static ip info failed");
        return;
    }

    esp_netif_dns_info_t dns = { 0 };
    dns.ip.u_addr.ip4.addr = EXAMPLE_MAIN_DNS;
    dns.ip.type = IPADDR_TYPE_V4;
    if (esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK) {
        ESP_LOGE(TAG, "set main dns info failed");
        return;
    }
    dns.ip.u_addr.ip4.addr = EXAMPLE_BACKUP_DNS;
    dns.ip.type = IPADDR_TYPE_V4;
    if (esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns) != ESP_OK) {
        ESP_LOGE(TAG, "set backup dns info failed");
        return;
    }

    ESP_LOGI(TAG, "set static ip success, ip:%s netmask:%s gw:%s mainDns:%s backupDns:%s",
        EXAMPLE_STATIC_IP, EXAMPLE_STATIC_NETMASK, EXAMPLE_STATIC_GW, EXAMPLE_MAIN_DNS, EXAMPLE_BACKUP_DNS);
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
            set_static_ip(arg);
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
            ESP_LOGI(TAG, "get static ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_event->ip_info.ip), IP2STR(&got_event->ip_info.netmask), IP2STR(&got_event->ip_info.gw));
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

    esp_netif_init();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, sta_netif, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, sta_netif, NULL);

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
