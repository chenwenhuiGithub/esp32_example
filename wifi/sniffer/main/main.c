#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define CONFIG_WIFI_SCAN_AP_SIZE            10
#define CONFIG_WIFI_CHANNEL_SIZE            14
#define CONFIG_WIFI_FRAME_FCS_LEN           4   


static const char *TAG = "sniffer";
static wifi_ap_record_t ap_records[CONFIG_WIFI_SCAN_AP_SIZE] = {0};
static uint16_t ap_size = CONFIG_WIFI_SCAN_AP_SIZE;
static uint16_t ap_num = 0;
static uint8_t ap_channels[CONFIG_WIFI_CHANNEL_SIZE] = {0};
static uint8_t ap_channel_num = 0;


static void sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    wifi_promiscuous_pkt_t *sniffer_pkt = (wifi_promiscuous_pkt_t *)buf;

    ESP_LOGI(TAG, "recv pkt, type(0-mgmt,1-ctrl,2-data):%u channel:%u len:%u",
        type, sniffer_pkt->rx_ctrl.channel, sniffer_pkt->rx_ctrl.sig_len - CONFIG_WIFI_FRAME_FCS_LEN);
    // ESP_LOG_BUFFER_HEX(TAG, sniffer_pkt->payload, sniffer_pkt->rx_ctrl.sig_len - CONFIG_WIFI_FRAME_FCS_LEN);
}

static void wifi_sniffer_task(void *pvParameters)
{
    wifi_promiscuous_filter_t sniffer_filter = {0};
    uint32_t i = 0;

    sniffer_filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&sniffer_filter);
    esp_wifi_set_promiscuous_rx_cb(sniffer_cb);
    esp_wifi_set_promiscuous(true);

    while (1) {
        esp_wifi_set_channel(ap_channels[i], WIFI_SECOND_CHAN_NONE);
        i++;
        if (ap_channel_num == i) {
            i = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    wifi_event_sta_connected_t* evt_sta_conn = NULL;
    wifi_event_sta_disconnected_t* evt_sta_dis = NULL;
    wifi_event_home_channel_change_t* evt_channel_change = NULL;
    ip_event_got_ip_t* evt_got_ip = NULL;
    uint32_t i = 0, j = 0;

    if (WIFI_EVENT == event_base) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            ESP_LOGI(TAG, "scan start");
            esp_wifi_scan_start(NULL, false); // nonblock
            ESP_LOGI(TAG, "scanning...");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            evt_sta_conn = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED, channel:%u authmode:%u", evt_sta_conn->channel, evt_sta_conn->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            evt_sta_dis = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "WIFI_EVENT_STA_DISCONNECTED, reason:%u", evt_sta_dis->reason);
            break;
        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "WIFI_EVENT_SCAN_DONE");
            esp_wifi_scan_get_ap_num(&ap_num);
            esp_wifi_scan_get_ap_records(&ap_size, ap_records);
            ESP_LOGI(TAG, "scanned ap num:%u", ap_num);
            for (i = 0; i < (ap_num > ap_size ? ap_size : ap_num); i++) {
                ESP_LOGI(TAG, "mac:"MACSTR" rssi:%d channel:%u ssid:%s", MAC2STR(ap_records[i].bssid), ap_records[i].rssi, ap_records[i].primary, ap_records[i].ssid);
                for (j = 0; j < ap_channel_num; j++) {
                    if (ap_channels[j] == ap_records[i].primary) { // repeat channel
                        break;
                    }
                }
                if (j == ap_channel_num) { // new channel
                    ap_channels[ap_channel_num] = ap_records[i].primary;
                    ap_channel_num++;
                }
            }
            xTaskCreate(wifi_sniffer_task, "wifi_sniffer_task", 4096, NULL, 5, NULL);
            break;
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
            evt_channel_change = (wifi_event_home_channel_change_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_HOME_CHANNEL_CHANGE, %u -> %u", evt_channel_change->old_chan, evt_channel_change->new_chan);
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

void app_main(void)
{
    esp_err_t err = ESP_OK;
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);

    esp_netif_init();
    esp_netif_create_default_wifi_sta();

    esp_wifi_init(&init_cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
