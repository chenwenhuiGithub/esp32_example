#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EXAMPLE_WIFI_SSID           "esp32_wroom_32E"
#define EXAMPLE_WIFI_PWD            "12345678"
#define EXAMPLE_WIFI_CHANNEL        1
#define EXAMPLE_WIFI_MAX_STA        3

static const char *TAG = "wifi_ap";

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    wifi_event_ap_staconnected_t* sta_conn_event = NULL;
    wifi_event_ap_stadisconnected_t* sta_disconn_event = NULL;

    if (event_base == WIFI_EVENT ) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            sta_conn_event = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "sta join, %02x:%02x:%02x:%02x:%02x:%02x AID:%u", sta_conn_event->mac[0], sta_conn_event->mac[1],
                sta_conn_event->mac[2], sta_conn_event->mac[3], sta_conn_event->mac[4], sta_conn_event->mac[5], sta_conn_event->aid);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            sta_disconn_event = (wifi_event_ap_stadisconnected_t*)event_data;
            ESP_LOGI(TAG, "sta leave, %02x:%02x:%02x:%02x:%02x:%02x, AID:%u", sta_disconn_event->mac[0], sta_disconn_event->mac[1],
                sta_disconn_event->mac[2], sta_disconn_event->mac[3], sta_disconn_event->mac[4], sta_disconn_event->mac[5], sta_disconn_event->aid);
        } else {
            ESP_LOGW(TAG, "unknown WIFI_EVENT:%d", event_id);
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

    esp_netif_init();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);

    wifi_config_t ap_config = {
        .ap = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PWD,
            .channel = EXAMPLE_WIFI_CHANNEL,
            .max_connection = EXAMPLE_WIFI_MAX_STA,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "wifi softap success, ssid:%s password:%s channel:%u", EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PWD, EXAMPLE_WIFI_CHANNEL);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
