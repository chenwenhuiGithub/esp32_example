#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"


#define EXAMPLE_WIFI_SSID               "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                "12345678"

#define MQTT_SECURE_TYPE_TCP            0x01 // not encrypt
#define MQTT_SECURE_TYPE_PSK            0x02 // psk
#define MQTT_SECURE_TYPE_TLS            0x03 // not check certificate
#define MQTT_SECURE_TYPE_AUTH           0x04 // check server certificate
#define MQTT_SECURE_TYPE_MUTUAL_AUTH    0x05 // check server and client certificate
#define MQTT_SECURE_TYPE                MQTT_SECURE_TYPE_MUTUAL_AUTH

#if (MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_AUTH || MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_MUTUAL_AUTH)
// server root cert, download from https://test.mosquitto.org/ssl/mosquitto.org.crt
static const char test_mosquitto_org_crt[]  =
"-----BEGIN CERTIFICATE-----\n"
"MIIEAzCCAuugAwIBAgIUBY1hlCGvdj4NhBXkZ/uLUZNILAwwDQYJKoZIhvcNAQEL"
"BQAwgZAxCzAJBgNVBAYTAkdCMRcwFQYDVQQIDA5Vbml0ZWQgS2luZ2RvbTEOMAwG"
"A1UEBwwFRGVyYnkxEjAQBgNVBAoMCU1vc3F1aXR0bzELMAkGA1UECwwCQ0ExFjAU"
"BgNVBAMMDW1vc3F1aXR0by5vcmcxHzAdBgkqhkiG9w0BCQEWEHJvZ2VyQGF0Y2hv"
"by5vcmcwHhcNMjAwNjA5MTEwNjM5WhcNMzAwNjA3MTEwNjM5WjCBkDELMAkGA1UE"
"BhMCR0IxFzAVBgNVBAgMDlVuaXRlZCBLaW5nZG9tMQ4wDAYDVQQHDAVEZXJieTES"
"MBAGA1UECgwJTW9zcXVpdHRvMQswCQYDVQQLDAJDQTEWMBQGA1UEAwwNbW9zcXVp"
"dHRvLm9yZzEfMB0GCSqGSIb3DQEJARYQcm9nZXJAYXRjaG9vLm9yZzCCASIwDQYJ"
"KoZIhvcNAQEBBQADggEPADCCAQoCggEBAME0HKmIzfTOwkKLT3THHe+ObdizamPg"
"UZmD64Tf3zJdNeYGYn4CEXbyP6fy3tWc8S2boW6dzrH8SdFf9uo320GJA9B7U1FW"
"Te3xda/Lm3JFfaHjkWw7jBwcauQZjpGINHapHRlpiCZsquAthOgxW9SgDgYlGzEA"
"s06pkEFiMw+qDfLo/sxFKB6vQlFekMeCymjLCbNwPJyqyhFmPWwio/PDMruBTzPH"
"3cioBnrJWKXc3OjXdLGFJOfj7pP0j/dr2LH72eSvv3PQQFl90CZPFhrCUcRHSSxo"
"E6yjGOdnz7f6PveLIB574kQORwt8ePn0yidrTC1ictikED3nHYhMUOUCAwEAAaNT"
"MFEwHQYDVR0OBBYEFPVV6xBUFPiGKDyo5V3+Hbh4N9YSMB8GA1UdIwQYMBaAFPVV"
"6xBUFPiGKDyo5V3+Hbh4N9YSMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEL"
"BQADggEBAGa9kS21N70ThM6/Hj9D7mbVxKLBjVWe2TPsGfbl3rEDfZ+OKRZ2j6AC"
"6r7jb4TZO3dzF2p6dgbrlU71Y/4K0TdzIjRj3cQ3KSm41JvUQ0hZ/c04iGDg/xWf"
"+pp58nfPAYwuerruPNWmlStWAXf0UTqRtg4hQDWBuUFDJTuWuuBvEXudz74eh/wK"
"sMwfu1HFvjy5Z0iMDU8PUDepjVolOCue9ashlS4EB5IECdSR2TItnAIiIwimx839"
"LdUdRudafMu5T5Xma182OC0/u/xRlEm+tvKGGmfFcN0piqVl8OrSPBgIlb+1IKJE"
"m/XriWr/Cq4h/JfB7NTsezVslgkBaoU=\n"
"-----END CERTIFICATE-----";
#endif

#if MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_MUTUAL_AUTH
// client private key, generate by openssl genrsa -out client.key 2048
static const char client_key[]  =
"-----BEGIN PRIVATE KEY-----\n"
"MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCmlO5QYz++1zeq"
"uk+vr1f0ywdlFsfLgQ3VNRHSBa+kDMOonuQs0UXXUn2faNXt4/vipIkvl8QcqHeD"
"4MLZiSyYCGG/WVNT81rrgA6p5o9P6BjbecZoAuU8H3Mj7Tzaj9Igyoclt0QpPVDT"
"dduFxY215Y21jSZ7/Jls4X+UddIYmv0gp1S9kMK+A6JanH9OOCpnhXH+3Ys19u7n"
"hIPjGVg/nrDHmX0DDIKhtsolJwgAs2TfP5U7JbuWkBe13Yi/BjVHU43jhsyd8jn2"
"N3twJVdoLfRO2TpnuzltR56ngq9l9cyKCj6vQEhrwswVTGihkf+vDxvVV3qs8z0u"
"07fuGB1TAgMBAAECggEACCFfZaUHyMoQTtDmcBi1c56FLzXmwn6mAjCjkgXyW1C+"
"1qK1WEqj3K02BxXC0JLpXwJ7f6B/UzbAaTtgceWeu5I30x20/Mqf4WalUJLkORjc"
"PboP06ZwhyKx0qBoRp1GZZCnpVDwjrgOEy6+wXoG6fMuRhzW3Vj9frGdhyLJAlJa"
"EScmGbmHF/9fZgdI+FNvmgRWTJQVIoBC5SsoD1t/PqkkR2wrFKXchg3IzR1BdT9W"
"lQfd6ycst/bXLreXikT212msnq03cR/E8DmzbwWLMb8lk5xD+NR+Uipa7EhRu0h8"
"3BAvCq3TublKGeJ+QG8ycVNbkdP8RC0Uys8h8A9sIQKBgQDTa4KpuTAf2aWJP6DK"
"1JbltGp29BP5bkolzc9zBG/WWzzhtlIIQsryUfxQMy5o3Xo9FUgwZd0Wt1pxXTes"
"tK/nFH6DrR8m+D5vadiwifaYaZkeS65fu0zst8e9BH+CMdlB1NGnDXszJJsQwmDK"
"KItuKPIIzIj9n6CTuLrtgcb6nwKBgQDJtQvB49w/vfe0JrhX/NomtEi8AmFIQz3N"
"Cn8vZsEYgJODry0BOWeQ2g4EHhd3/NmpoHkhyk/Nz6o/I39DS2P7d56dVs4G4x/U"
"ywMMXKkStniBRsgSt0c0YSTFCl6S0wpIZWyh3EjkcLD57Dd6z3PS5V6F+V5802Cr"
"mrRsJ7MUzQKBgQCV0fgUIiGSUH/YFPjzA1ezi/huN2T4O0ncJE6/6QL/2kP4h2T2"
"aa67rZGpm7tloJ2BL9WqRmU4NdKnxzEu+BQ3IvTMhyAuU3ibJ1zhLcNMGnjhWSxG"
"tso6bbnjno5lSsH0vsWbJhiKE4S3iadhWv4DoIxz482oPThjtmLw8Ch7bQKBgHQm"
"0EVvnEYMc5aG8YKhdVHyYSv6xuBg8Dahg8ndBvbAG7Ip7uWUk+Hi301ZsrQCo6i9"
"YuOlZ70hh2ziWQd8Y2/MW0dJVEy8/3h/CxtURHwlHVF8W/wDFHrCMfVRWlJ8OqCj"
"3yF6A3OgvRi+ANi0m4LnWD4X8mQ5KKLI7HqXhdVFAoGBAKvaiCCx+WWWKK3DFWiY"
"6UpUYAuQfY0oVB/s01EmUeSfQ5i5mBLY6Me0ou6ZwOOpGbKr9NuLbUbj2uEDo9vV"
"np0O6MVqHBeCfm0Ehj4mjqveMl+Cs1cOfvqnVZl4a+6/A7Zh14YHCZIB1+t7aoFN"
"E2cRBap1OYNpEe5C/ON3IoVi\n"
"-----END PRIVATE KEY-----";

// client cert
// 1. generate csr by openssl req -new -key client.key -out client.csr -subj "/C=cn/ST=zhejiang/L=hangzhou/O=jingdong/OU=beijing jingdong company/CN=www.jd.com"
// 2. genarate cert from https://test.mosquitto.org/ssl/index.php
static const char client_crt[]  =
"-----BEGIN CERTIFICATE-----\n"
"MIIDpDCCAoygAwIBAgIBADANBgkqhkiG9w0BAQsFADCBkDELMAkGA1UEBhMCR0Ix"
"FzAVBgNVBAgMDlVuaXRlZCBLaW5nZG9tMQ4wDAYDVQQHDAVEZXJieTESMBAGA1UE"
"CgwJTW9zcXVpdHRvMQswCQYDVQQLDAJDQTEWMBQGA1UEAwwNbW9zcXVpdHRvLm9y"
"ZzEfMB0GCSqGSIb3DQEJARYQcm9nZXJAYXRjaG9vLm9yZzAeFw0yNDA2MTUxMzM1"
"MTNaFw0yNDA5MTMxMzM1MTNaMH4xCzAJBgNVBAYTAmNuMREwDwYDVQQIDAh6aGVq"
"aWFuZzERMA8GA1UEBwwIaGFuZ3pob3UxETAPBgNVBAoMCGppbmdkb25nMSEwHwYD"
"VQQLDBhiZWlqaW5nIGppbmdkb25nIGNvbXBhbnkxEzARBgNVBAMMCnd3dy5qZC5j"
"b20wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCmlO5QYz++1zequk+v"
"r1f0ywdlFsfLgQ3VNRHSBa+kDMOonuQs0UXXUn2faNXt4/vipIkvl8QcqHeD4MLZ"
"iSyYCGG/WVNT81rrgA6p5o9P6BjbecZoAuU8H3Mj7Tzaj9Igyoclt0QpPVDTdduF"
"xY215Y21jSZ7/Jls4X+UddIYmv0gp1S9kMK+A6JanH9OOCpnhXH+3Ys19u7nhIPj"
"GVg/nrDHmX0DDIKhtsolJwgAs2TfP5U7JbuWkBe13Yi/BjVHU43jhsyd8jn2N3tw"
"JVdoLfRO2TpnuzltR56ngq9l9cyKCj6vQEhrwswVTGihkf+vDxvVV3qs8z0u07fu"
"GB1TAgMBAAGjGjAYMAkGA1UdEwQCMAAwCwYDVR0PBAQDAgXgMA0GCSqGSIb3DQEB"
"CwUAA4IBAQB9tNwRsZuuLMLhLe5ChSkcVIK8PrPkGmxbuq7XUQ6ktdyy8dqJwNYk"
"2oqmtU6UnGd24B+bT3w6JkCPTvZu2xxkJfXFAli0R2QUyedSTrksm8uMM0PccqMo"
"PfFYczWvm+w7BGWxc4HIaa/E4WYeFK9oSr9HooAtFpURB3TZPZqwxsKe+3LwgyJM"
"/KauccW/+alTmNkbBAQqKi0jA71C+R0wtokkcB35H/sw/TxpFbOluAaBo5YnCCqV"
"7+TrBtbrigLJZOR/+sVjrSyJc+EjCNXF0xADVHTtoUQHGU6g2anMNhnktPr/miDj"
"c0TkbsY+MdSX2TJxXaaA8uFFvmH/ejW9\n"
"-----END CERTIFICATE-----";
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
        .broker.verification.certificate = test_mosquitto_org_crt,
#elif MQTT_SECURE_TYPE == MQTT_SECURE_TYPE_MUTUAL_AUTH
        .broker.address.uri = "mqtts://test.mosquitto.org",
        .broker.address.port = 8883,
        .broker.verification.certificate = test_mosquitto_org_crt,
        .credentials.authentication.certificate = client_crt,
        .credentials.authentication.key = client_key,
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
