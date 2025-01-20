#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#define CONFIG_WIFI_SSID_SIZE                   32
#define CONFIG_WIFI_PWD_SIZE                    64
#define CONFIG_WIFI_SCAN_AP_SIZE                10
#define CONFIG_WIFI_CHANNEL_SIZE                14
#define CONFIG_WIFI_FRAME_HEADER_LEN            24
#define CONFIG_WIFI_FRAME_FCS_LEN               4   
#define CONFIG_WIFI_FRAME_ADDR1_INDEX           4
#define CONFIG_AIRKISS_LEAD_SIZE                4
#define CONFIG_AIRKISS_MAGIC_SIZE               4
#define CONFIG_AIRKISS_PREFIX_SIZE              4
#define CONFIG_AIRKISS_SEQ_SIZE                 6
#define CONFIG_AIRKISS_MAGIC_MASK               0x01f0
#define CONFIG_AIRKISS_PREFIX_MASK              0x01f0
#define CONFIG_AIRKISS_SEQ_HEADER_MASK          0x0180
#define CONFIG_AIRKISS_SEQ_DATA_MASK            0x0100
#define CONFIG_AIRKISS_BOARDCAST_UDP_ADDR       "255.255.255.255"
#define CONFIG_AIRKISS_BOARDCAST_UDP_PORT       10000
#define CONFIG_AIRKISS_BOARDCAST_TIMES          3

typedef enum {
    AIRKISS_STAT_LEAD = 0,
    AIRKISS_STAT_MAGIC,
    AIRKISS_STAT_PREFIX,
    AIRKISS_STAT_SEQ,
    AIRKISS_STAT_DONE
} airkiss_stat_t;

static const char *TAG = "airkiss";
static uint8_t payload[CONFIG_WIFI_PWD_SIZE + 1 + CONFIG_WIFI_SSID_SIZE] = {0}; // pwd + rand(1B) + ssid
static char ssid[CONFIG_WIFI_SSID_SIZE + 1] = {0};
static char pwd[CONFIG_WIFI_PWD_SIZE + 1] = {0};
static uint8_t rand_value = 0;

static wifi_ap_record_t ap_records[CONFIG_WIFI_SCAN_AP_SIZE] = {0};
static uint16_t ap_size = CONFIG_WIFI_SCAN_AP_SIZE;
static uint16_t ap_num = 0;
static uint8_t ap_channels[CONFIG_WIFI_CHANNEL_SIZE] = {0};
static uint8_t ap_channel_num = 0;

static airkiss_stat_t airkiss_stat = AIRKISS_STAT_LEAD;
static uint16_t lead[CONFIG_AIRKISS_LEAD_SIZE] = {0};
static uint8_t lead_num = 0;
static uint8_t lead_valid = 0;
static uint16_t delta = 0;

static uint16_t magic[CONFIG_AIRKISS_MAGIC_SIZE] = {0};
static uint8_t magic_num = 0;
static uint8_t total_len = 0;
static uint8_t ssid_crc = 0;

static uint16_t prefix[CONFIG_AIRKISS_PREFIX_SIZE] = {0};
static uint8_t prefix_num = 0;
static uint8_t pwd_len = 0;
static uint8_t pwd_len_crc = 0;

static uint16_t seq[CONFIG_AIRKISS_SEQ_SIZE] = {0};
static uint8_t seq_num = 0;
static uint8_t seq_quotient = 0;   // total_len / 4
static uint8_t seq_remainder = 0;  // total_len % 4
static uint32_t seq_bitmap = 0;    // seq_quotient bit

/*
1. 获取当前环境中 ssid、rssi、channel(非必要，但可节省配网时间)
2. 切换信道
3. 接收 lead code，获取长度差值 delta
4. 如果接收 lead code 超时则跳转到 2，否则跳转到 5
5. 接收 magic code
            | bit8   | bit7   | bit6~4  | bit3~0            |
    pkg 1   | 0      | 0      | 000     | total_len(high)   |
    pkg 2   | 0      | 0      | 001     | total_len(low)    |
    pkg 3   | 0      | 0      | 010     | ssid_crc(high)    |
    pkg 4   | 0      | 0      | 011     | ssid_crc(low)     |
6. 接收 prefix code
            | bit8   | bit7   | bit6~4  | bit3~0            |
    pkg 1   | 0      | 0      | 100     | pwd_len(high)     |
    pkg 2   | 0      | 0      | 101     | pwd_len(low)      |
    pkg 3   | 0      | 0      | 110     | pwd_len_crc(high) |
    pkg 4   | 0      | 0      | 111     | pwd_len_crc(low)  |
7. 接收 seq
            | bit8   | bit7   | bit6~0                      |
    pkg 1   | 0      | 1      | seq_crc(low 7bit)           |
    pkg 2   | 0      | 1      | seq_index                   |
    pkg 3   | 1      | data1                                |
    pkg 4   | 1      | data2                                |
    pkg 5   | 1      | data3                                |
    pkg 6   | 1      | data4                                |
8. 设备连接路由器，向 10000 端口广播 random + mac
*/

static uint8_t calc_crc8maxim(uint8_t *data, uint8_t len) {
    uint8_t i = 0, j = 0, crc = 0;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x01) {
                crc = (crc >> 1) ^ 0x8c;
            } else {
                crc >>= 1;
            } 
        }
    }
    return crc;
}

static uint8_t airkiss_check_leading() {
    uint8_t is_valid = 0;

    if ((lead[1] - lead[0] == 1) && (lead[2] - lead[1] == 1) && (lead[3] - lead[2] == 1)) { // 4 consecutive value
        is_valid = 1;
    }
    return is_valid;
}

static uint8_t airkiss_check_magic() {
    uint8_t is_valid = 0;
    uint8_t len = 0;

    if (0x0000 == (magic[0] & CONFIG_AIRKISS_MAGIC_MASK) &&
        0x0010 == (magic[1] & CONFIG_AIRKISS_MAGIC_MASK) &&
        0x0020 == (magic[2] & CONFIG_AIRKISS_MAGIC_MASK) &&
        0x0030 == (magic[3] & CONFIG_AIRKISS_MAGIC_MASK)) {

        if ((magic[0] & 0x000f) == 0x0008) { // total_len < 16
            len |= 0x0f;
        } else {
            len |= (magic[0] & 0x000f) << 4;    
        }
        len |= magic[1] & 0x000f;
        if (len <= (CONFIG_WIFI_PWD_SIZE + 1 + CONFIG_WIFI_SSID_SIZE)) {
            is_valid = 1;
        }
    }
    return is_valid;
}

static uint8_t airkiss_check_prefix() {
    uint8_t is_valid = 0;

    if (0x0040 == (prefix[0] & CONFIG_AIRKISS_PREFIX_MASK) &&
        0x0050 == (prefix[1] & CONFIG_AIRKISS_PREFIX_MASK) &&
        0x0060 == (prefix[2] & CONFIG_AIRKISS_PREFIX_MASK) &&
        0x0070 == (prefix[3] & CONFIG_AIRKISS_PREFIX_MASK)) {
        is_valid = 1;
    }
    return is_valid;
}

static uint8_t airkiss_check_sequence() {
    uint8_t is_valid = 0;
    uint8_t i = 0, seq_index = 0, seq_data_len = 4, calc_crc = 0;
    uint8_t data[CONFIG_AIRKISS_SEQ_SIZE] = {0};

    if (0x0080 == (seq[0] & CONFIG_AIRKISS_SEQ_HEADER_MASK) &&
        0x0080 == (seq[1] & CONFIG_AIRKISS_SEQ_HEADER_MASK)) { // seq header valid
        seq_index = (uint8_t)(seq[1] & 0x007f);
        if (seq_index == seq_quotient - 1) { // last seq
            seq_data_len = seq_remainder;
        }

        for (i = 0; i < seq_data_len; i++) {
            if (0x0100 != (seq[2 + i] & CONFIG_AIRKISS_SEQ_DATA_MASK)) {
                break;
            }
        }
        if (i == seq_data_len) { // seq data valid
            data[0] = (uint8_t)(seq[0] & 0x007f); // seq_crc, low 7bit
            data[1] = (uint8_t)(seq[1] & 0x007f); // seq_index, low 7bit
            for (i = 0; i < seq_data_len; i++) {
                data[2 + i] = (uint8_t)seq[2 + i];
            }
            calc_crc = calc_crc8maxim(data + 1, seq_data_len + 1); // crc8maxim(seq_index data[0] ...)
            if ((calc_crc & 0x7f) == data[0]) {
                is_valid = 1;
            }
        }
    }
    return is_valid;
}

static void airkiss_process(uint16_t frame_len) {
    uint16_t ori_len = 0;
    uint8_t magic_valid = 0, prefix_valid = 0, sequence_valid = 0;
    uint8_t i = 0, seq_index = 0, seq_data_len = 4;

    switch (airkiss_stat) {
    case AIRKISS_STAT_LEAD:
        if (lead_num < CONFIG_AIRKISS_LEAD_SIZE) {
            lead[lead_num] = frame_len;
            lead_num++;
            if (CONFIG_AIRKISS_LEAD_SIZE == lead_num) {
                lead_valid = airkiss_check_leading();
            }
        } else {
            lead[0] = lead[1]; // overflow first value
            lead[1] = lead[2];
            lead[2] = lead[3];
            lead[3] = frame_len;
            lead_valid = airkiss_check_leading();       
        }

        if (lead_valid) {
            delta = lead[0] - 1;
            airkiss_stat = AIRKISS_STAT_MAGIC;
            ESP_LOGI(TAG, "lead done, delta:%u", delta);
        }
        break;
    case AIRKISS_STAT_MAGIC:
        ori_len = frame_len - delta;
        if ((ori_len & CONFIG_AIRKISS_MAGIC_MASK) <= 0x0030) {
            if (magic_num < CONFIG_AIRKISS_MAGIC_SIZE) {
                magic[magic_num] = ori_len;
                magic_num++;
                if (CONFIG_AIRKISS_MAGIC_SIZE == magic_num) {
                    magic_valid = airkiss_check_magic();
                }
            } else {
                magic[0] = magic[1]; // overflow first value
                magic[1] = magic[2];
                magic[2] = magic[3];
                magic[3] = ori_len;
                magic_valid = airkiss_check_magic();
            }

            if (magic_valid) {
                if ((magic[0] & 0x000f) == 0x0008) { // total_len < 16
                    total_len |= 0x0f;
                } else {
                    total_len |= (magic[0] & 0x000f) << 4;    
                }
                total_len |= magic[1] & 0x000f;
                ssid_crc |= (magic[2] & 0x000f) << 4;
                ssid_crc |= magic[3] & 0x000f;

                seq_quotient = total_len / 4;
                seq_remainder = total_len % 4;
                if (seq_remainder) {
                    seq_quotient++;
                }
                for (i = 0; i < seq_quotient; i++) { // all seq data not saved
                    seq_bitmap |= (1 << i);
                }

                airkiss_stat = AIRKISS_STAT_PREFIX;
                ESP_LOGI(TAG, "magic done, total_len:%u, ssid_crc:0x%02x", total_len, ssid_crc);
            }
        }
        break;
    case AIRKISS_STAT_PREFIX:
        ori_len = frame_len - delta;
        if (0x0040 <= (ori_len & CONFIG_AIRKISS_PREFIX_MASK) && (ori_len & CONFIG_AIRKISS_PREFIX_MASK) <= 0x0070) {
            if (prefix_num < CONFIG_AIRKISS_PREFIX_SIZE) {
                prefix[prefix_num] = ori_len;
                prefix_num++;
                if (CONFIG_AIRKISS_PREFIX_SIZE == prefix_num) {
                    prefix_valid = airkiss_check_prefix();
                }
            } else {
                prefix[0] = prefix[1]; // overflow first value
                prefix[1] = prefix[2];
                prefix[2] = prefix[3];
                prefix[3] = ori_len;
                prefix_valid = airkiss_check_prefix();
            }

            if (prefix_valid) {
                pwd_len |= (prefix[0] & 0x000f) << 4;                
                pwd_len |= prefix[1] & 0x000f;
                pwd_len_crc |= (prefix[2] & 0x000f) << 4;
                pwd_len_crc |= prefix[3] & 0x000f;
                airkiss_stat = AIRKISS_STAT_SEQ;
                ESP_LOGI(TAG, "prefix done, pwd_len:%u, pwd_len_crc:0x%02x", pwd_len, pwd_len_crc);
            }
        }
        break;
    case AIRKISS_STAT_SEQ:
        ori_len = frame_len - delta;
        if (0x0080 == (ori_len & CONFIG_AIRKISS_SEQ_HEADER_MASK) ||  0x0100 == (ori_len & CONFIG_AIRKISS_SEQ_DATA_MASK)) {
            if (seq_num < CONFIG_AIRKISS_SEQ_SIZE) {
                seq[seq_num] = ori_len;
                seq_num++;
                if (CONFIG_AIRKISS_SEQ_SIZE == seq_num) {
                    sequence_valid = airkiss_check_sequence();
                }
            } else {
                seq[0] = seq[1]; // overflow first value
                seq[1] = seq[2];
                seq[2] = seq[3];
                seq[3] = seq[4];
                seq[4] = seq[5];
                seq[5] = ori_len;
                sequence_valid = airkiss_check_sequence();
            }

            if (sequence_valid) {
                seq_num = 0; // continue recv next seq
                seq_index = (uint8_t)(seq[1] & 0x007f);
                if (seq_bitmap & (1 << seq_index)) { // seq data not saved yet
                    if (seq_index == seq_quotient - 1) { // last seq
                        seq_data_len = seq_remainder;
                    }
                    for (i = 0; i < seq_data_len; i++) {
                        payload[seq_index * 4 + i] = (uint8_t)seq[2 + i];
                    }
                    seq_bitmap &= ~(1 << seq_index);
                }

                if (!seq_bitmap) { // all seq data saved
                    esp_wifi_set_promiscuous(false);
                    memcpy(ssid, payload + pwd_len + 1, total_len - pwd_len - 1);
                    memcpy(pwd, payload, pwd_len);
                    rand_value = payload[pwd_len];
                    airkiss_stat = AIRKISS_STAT_DONE;
                    ESP_LOGI(TAG, "seq done, random:0x%02x ssid:%s pwd:%s", rand_value, ssid, pwd);

                    wifi_config_t sta_config = {0};
                    memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
                    memcpy(sta_config.sta.password, pwd, strlen(pwd));
                    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
                    esp_wifi_connect(); // non-block
                    ESP_LOGI(TAG, "start wifi connect to %s", ssid);
                }
            }
        }
        break;
    case AIRKISS_STAT_DONE:
        break;
    default:
        ESP_LOGW(TAG, "unknown airkiss_stat:%u", airkiss_stat);
        break;
    }
}

static void airkiss_boardcast() {
    int sock = 0;
    uint8_t i = 0;
    uint8_t data[7] = {0}; // random(1B) + mac
    struct sockaddr_in server_addr = {0};

    data[0] = rand_value;
    esp_wifi_get_mac(WIFI_IF_STA, data + 1);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(CONFIG_AIRKISS_BOARDCAST_UDP_ADDR);
    server_addr.sin_port = htons(CONFIG_AIRKISS_BOARDCAST_UDP_PORT);
    for (i = 0; i < CONFIG_AIRKISS_BOARDCAST_TIMES; i++) {
        ESP_LOGI(TAG, "boardcast ack, times:%u", i);
        sendto(sock, data, sizeof(data), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    uint8_t boardcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    wifi_promiscuous_pkt_t *sniffer_pkt = (wifi_promiscuous_pkt_t *)buf;

    if (WIFI_PKT_DATA == type) {
        if (sniffer_pkt->rx_ctrl.sig_len - CONFIG_WIFI_FRAME_FCS_LEN > CONFIG_WIFI_FRAME_HEADER_LEN) {
            if ((sniffer_pkt->payload[1] & 0x03) == 0x02) { // ToDS - 0, FromDS - 1
                if (memcmp(sniffer_pkt->payload + CONFIG_WIFI_FRAME_ADDR1_INDEX, boardcast_mac, 6) == 0) { // boardcast pkt
                    ESP_LOGI(TAG, "recv pkt len:%u", sniffer_pkt->rx_ctrl.sig_len - CONFIG_WIFI_FRAME_FCS_LEN);
                    airkiss_process(sniffer_pkt->rx_ctrl.sig_len - CONFIG_WIFI_FRAME_FCS_LEN);
                }
            }
        }
    }
}

static void sniffer_task(void *pvParameters) {
    wifi_promiscuous_filter_t sniffer_filter = {0};
    uint8_t i = 0;

    sniffer_filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&sniffer_filter);
    esp_wifi_set_promiscuous_rx_cb(sniffer_cb);

    while (!lead_valid) {
        esp_wifi_set_promiscuous(false);
        lead_num = 0; // clear previous channel lead
        esp_wifi_set_channel(ap_channels[i], WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);

        i++;
        if (ap_channel_num == i) {
            i = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
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
            xTaskCreate(sniffer_task, "sniffer_task", 4096, NULL, 5, NULL);
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
            airkiss_boardcast();
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
