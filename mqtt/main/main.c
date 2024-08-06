#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"


#define EXAMPLE_WIFI_SSID                       "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                        "12345678"

#define MQTT_SECURE_TYPE_TCP                    0x01 // not encrypt
#define MQTT_SECURE_TYPE_PSK                    0x02 // psk
#define MQTT_SECURE_TYPE_TLS                    0x03 // not check certificate
#define MQTT_SECURE_TYPE_AUTH                   0x04 // check server certificate
#define MQTT_SECURE_TYPE_MUTUAL_AUTH            0x05 // check server and client certificate
#define MQTT_SECURE_TYPE                        MQTT_SECURE_TYPE_MUTUAL_AUTH


#if (MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_AUTH || MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_MUTUAL_AUTH)
// server root cert, download from https://test.mosquitto.org/ssl/mosquitto.org.crt
extern const char mosquitto_cert_pem_start[]    asm("_binary_mosquitto_cert_pem_start");
extern const char mosquitto_cert_pem_end[]      asm("_binary_mosquitto_cert_pem_end");
#endif

#if MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_MUTUAL_AUTH
// generate client private key: openssl genrsa -out client.key 2048
extern const char client_prvtkey_pem_start[]    asm("_binary_client_prvtkey_pem_start");
extern const char client_prvtkey_pem_end[]      asm("_binary_client_prvtkey_pem_end");

// client cert
// 1. generate csr: openssl req -new -key client.key -out client.csr -subj "/C=cn/ST=zhejiang/L=hangzhou/O=jingdong/OU=beijing jingdong company/CN=www.jd.com"
// 2. genarate cert from https://test.mosquitto.org/ssl/index.php
extern const char client_cert_pem_start[]       asm("_binary_client_cert_pem_start");
extern const char client_cert_pem_end[]         asm("_binary_client_cert_pem_end");
#endif

static const char *TAG = "mqtt";
int msg_id[3] = {0};


static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id[0] = esp_mqtt_client_subscribe(client, "/cmd/down/sn123456", 0);
        msg_id[1] = esp_mqtt_client_subscribe(client, "/echo/down/sn123456", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGE(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (event->msg_id == msg_id[0]) {
            ESP_LOGI(TAG, "topic /cmd/down/sn123456 subscribe success");
        }
        if (event->msg_id == msg_id[1]) {
            ESP_LOGI(TAG, "topic /echo/down/sn123456 subscribe success");
        }
        break;
    case MQTT_EVENT_PUBLISHED:
        if (event->msg_id == msg_id[2]) {
            ESP_LOGI(TAG, "topic /echo/up/sn123456 publish success");
        }
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        if (!memcmp(event->topic, "/echo/down/sn123456", strlen("/echo/down/sn123456"))) {
            msg_id[2] = esp_mqtt_client_publish(client, "/echo/up/sn123456", event->data, event->data_len, 1, 0);           
        }
        break;
    default:
        ESP_LOGW(TAG, "unknown MQTT_EVENT:%d", event->event_id);
        break;
    }
}

static void start_mqtt_task()
{
    esp_mqtt_client_config_t mqtt_cfg = {
#if MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_TCP
        .broker.address.uri = "mqtt://test.mosquitto.org",
        .broker.address.port = 1883,
#elif MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_PSK

#elif MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_TLS

#elif MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_AUTH
        .broker.address.uri = "mqtts://test.mosquitto.org",
        .broker.address.port = 8883,
        .broker.verification.certificate = mosquitto_cert_pem_start,
#elif MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_MUTUAL_AUTH
        .broker.address.uri = "mqtts://test.mosquitto.org",
        .broker.address.port = 8883,
        .broker.verification.certificate = mosquitto_cert_pem_start,
        .credentials.authentication.certificate = client_cert_pem_start,
        .credentials.authentication.key = client_prvtkey_pem_start,
#endif
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
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
            start_mqtt_task();
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
    esp_wifi_start(); // trigger WIFI_EVENT_STA_START

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
