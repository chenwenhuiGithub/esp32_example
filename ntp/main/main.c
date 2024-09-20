#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "lwip/netdb.h"


#define EXAMPLE_WIFI_SSID                       "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                        "12345678"
#define EXAMPLE_NTP_SERVER                      "pool.ntp.org"
#define EXAMPLE_NTP_PORT                        123
#define EXAMPLE_NTP_LOCAL_PORT                  6007

typedef struct {  
    uint8_t flags;              // Leap Indicator, Version Number, Mode
    uint8_t stratum;            // Stratum level of the local clock
    uint8_t poll;               // Polling interval
    uint8_t precision;          // Precision of the local clock
    uint32_t rootDelay;         // Total round trip delay time
    uint32_t rootDispersion;    // Max error allowed from primary clock
    uint32_t refId;             // Reference clock identifier
    uint8_t refTimestamp[8];    // Reference time stamp
    uint8_t origTimestamp[8];   // Originate time stamp
    uint8_t recvTimestamp[8];   // Receive time stamp
    uint8_t sendTimestamp[8];   // Transmit time stamp
} ntp_packet_t;

static const char *TAG = "ntp";

static void get_server_ip(char *ip, uint32_t size) {
    struct addrinfo hints = {0}, *res = NULL;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(EXAMPLE_NTP_SERVER, NULL, &hints, &res)) {
        ESP_LOGE(TAG, "socket getaddrinfo failed:%d", errno);
        return;        
    }
    inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, size);
    freeaddrinfo(res);
}

static void get_ntp_timestamp() {
    int sock = 0;
    struct sockaddr_in local_addr = {0};
    struct sockaddr_in server_addr = {0};
    ntp_packet_t packet = {0};  
    socklen_t addr_len = sizeof(server_addr);
    char server_ip[INET_ADDRSTRLEN] = {0};
    struct timeval tv = {0};
    uint32_t t3_s = 0, t3_us = 0;
    time_t now = 0;
    char buf[64] = {0};
    struct tm timeinfo = {0};

    get_server_ip(server_ip, sizeof(server_ip));
    ESP_LOGI(TAG, "%s:%s", EXAMPLE_NTP_SERVER, server_ip);

    setenv("TZ", "CST-8", 1);
    tzset();

    gettimeofday(&tv, NULL);
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "before ntp:%s.%03lu", buf, tv.tv_usec / 1000);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        return;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(EXAMPLE_NTP_LOCAL_PORT);
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr))) {
        ESP_LOGE(TAG, "socket bind failed:%d", errno);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(EXAMPLE_NTP_PORT);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    packet.flags = (0x03 << 3) | 0x03; // NTP version 3, client mode
    if (sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "socket send failed:%d", errno);
        return;
    }

    if (recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr *)&server_addr, &addr_len) < 0) {
        ESP_LOGE(TAG, "socket recv failed:%d", errno);
        return;
    }

    close(sock);

    // now = (t4 + t3 + t2 - t1) / 2
    t3_s = (packet.sendTimestamp[0] << 24) | (packet.sendTimestamp[1] << 16) | (packet.sendTimestamp[2] << 8) | packet.sendTimestamp[3];
    t3_us = (packet.sendTimestamp[4] << 24) | (packet.sendTimestamp[5] << 16) | (packet.sendTimestamp[6] << 8) | packet.sendTimestamp[7];
    tv.tv_sec = t3_s - 2208988800; // NTP timestamp(1900.01.01 00:00:00) to Unix timestamp(1970.01.01 00:00:00)
    tv.tv_usec = ((uint64_t)t3_us * 1000000) >> 32; // fraction to microseconds  
    settimeofday(&tv, NULL);

    memset(buf, 0, sizeof(buf));
    gettimeofday(&tv, NULL);
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, " after ntp:%s.%03lu", buf, tv.tv_usec / 1000);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
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
            ESP_LOGI(TAG, "got ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_event->ip_info.ip), IP2STR(&got_event->ip_info.netmask), IP2STR(&got_event->ip_info.gw));
            get_ntp_timestamp();
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

void app_main(void) {
    esp_err_t err = ESP_OK;
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PWD,
        },
    };
    uint32_t i = 0;

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
    esp_wifi_init(&wifi_init_cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();

    while (1) {
        ESP_LOGI(TAG, "%lu", i++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
