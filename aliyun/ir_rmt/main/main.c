#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "mbedtls/md.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "ir_rmt.h"


#define EXAMPLE_WIFI_AP_IP                      "192.168.10.10"
#define EXAMPLE_WIFI_AP_NETMASK                 "255.255.255.0"
#define EXAMPLE_WIFI_AP_CHANNEL                 5
#define EXAMPLE_WIFI_AP_MAX_CONN                2

#define EXAMPLE_LED_GPIO_NUM                    2

#define EXAMPLE_ALIYUN_PK                       "a1GCY1V8kBX"
#define EXAMPLE_ALIYUN_DK                       "ovHa9DNEP3ma1WZs6aNE"
#define EXAMPLE_ALIYUN_DS                       "e36742c9698a83e63cf05c691a4bcc07"
#define EXAMPLE_MQTT_URL                        "mqtts://"EXAMPLE_ALIYUN_PK".iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define EXAMPLE_MQTT_PORT                       1883
#define EXAMPLE_MQTT_KEEP_ALIVE                 300 // 5min
#define EXAMPLE_MQTT_TOPIC_POST                 "/sys/"EXAMPLE_ALIYUN_PK"/"EXAMPLE_ALIYUN_DK"/thing/event/property/post"
#define EXAMPLE_MQTT_TOPIC_POST_REPLY           "/sys/"EXAMPLE_ALIYUN_PK"/"EXAMPLE_ALIYUN_DK"/thing/event/property/post_reply"
#define EXAMPLE_MQTT_TOPIC_SET                  "/sys/"EXAMPLE_ALIYUN_PK"/"EXAMPLE_ALIYUN_DK"/thing/service/property/set"
#define EXAMPLE_MQTT_TOPIC_SET_REPLY            "/sys/"EXAMPLE_ALIYUN_PK"/"EXAMPLE_ALIYUN_DK"/thing/service/property/set_reply"
#define EXAMPLE_MQTT_TOPIC_OTA_VERSION          "/ota/device/inform/"EXAMPLE_ALIYUN_PK"/"EXAMPLE_ALIYUN_DK
#define EXAMPLE_MQTT_TOPIC_OTA_TASK             "/ota/device/upgrade/"EXAMPLE_ALIYUN_PK"/"EXAMPLE_ALIYUN_DK
#define EXAMPLE_MQTT_TOPIC_OTA_PROGRESS         "/ota/device/progress/"EXAMPLE_ALIYUN_PK"/"EXAMPLE_ALIYUN_DK
#define EXAMPLE_TIMER_PERIOD_REPORT_PROGRESS    3000  // 3s


typedef struct {
    char url[256];
    char version[16];
    char sha256[128];
    uint32_t size;
    uint32_t download_size;
} ota_task_info_t;

static const char *TAG = "ir_rmt";

extern const uint8_t global_sign_crt_start[]    asm("_binary_global_sign_crt_start");
extern const uint8_t global_sign_crt_end[]      asm("_binary_global_sign_crt_end");

extern const uint8_t index_html_gz_start[]      asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]        asm("_binary_index_html_gz_end");

static esp_mqtt_client_handle_t hd_mqtt = NULL;
static esp_ota_handle_t hd_ota = 0;
static esp_http_client_handle_t hd_http = NULL;
static httpd_handle_t hd_httpd = NULL;
static nvs_handle_t hd_nvs = 0;
static int subscribe_id[2] = {0};
static ota_task_info_t ota_task = {0};
static char sta_ssid[32] = {0};
static char sta_pwd[64] = {0};
static size_t sta_ssid_len = 0;
static size_t sta_pwd_len = 0;
static uint8_t network_stat = 0; // 0 - not connected wifi, 1 - connected wifi, 2 - connected cloud

static uint8_t channel_id = 0;
static uint8_t rmt_id = 0;

static void read_cfg_from_flash() {
    nvs_open("ir_rmt", NVS_READWRITE, &hd_nvs);
    sta_ssid_len = sizeof(sta_ssid);
    sta_pwd_len = sizeof(sta_pwd);
    nvs_get_str(hd_nvs, "sta_ssid", sta_ssid, &sta_ssid_len);
    nvs_get_str(hd_nvs, "sta_pwd", sta_pwd, &sta_pwd_len);
    nvs_close(hd_nvs);
}

static void save_cfg_to_flash() {
    nvs_open("ir_rmt", NVS_READWRITE, &hd_nvs);
    nvs_set_str(hd_nvs, "sta_ssid", sta_ssid);
    nvs_set_str(hd_nvs, "sta_pwd", sta_pwd);
    nvs_commit(hd_nvs);
    nvs_close(hd_nvs);
}

static char *gen_post_msg_id() {
    static mbedtls_entropy_context entropy;
    static mbedtls_ctr_drbg_context ctr_drbg;
    static uint8_t init_flag = 0;
    uint32_t rand = 0;
    static char buf[16] = {0};

    if (!init_flag) {
        init_flag = 1;
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    }
    mbedtls_ctr_drbg_random(&ctr_drbg, (unsigned char *)&rand, sizeof(rand));
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%lu", rand);

    // mbedtls_ctr_drbg_free(&ctr_drbg);
    // mbedtls_entropy_free(&entropy);
    return buf;
}

static void gen_mqtt_credential(char *client_id, char *username, char *password) {
    char buf[256] = {0};
    uint8_t hmac[32] = {0};
    mbedtls_md_context_t md_ctx;
    uint32_t i = 0;

    sprintf(buf, "clientId%sdeviceName%sproductKey%s", EXAMPLE_ALIYUN_DK, EXAMPLE_ALIYUN_DK, EXAMPLE_ALIYUN_PK);

    mbedtls_md_init(&md_ctx);
    mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md_ctx, (unsigned char *)EXAMPLE_ALIYUN_DS, strlen(EXAMPLE_ALIYUN_DS));
    mbedtls_md_hmac_update(&md_ctx, (unsigned char *)buf, strlen(buf));
    mbedtls_md_hmac_finish(&md_ctx, hmac);
    mbedtls_md_free(&md_ctx);

    sprintf(client_id, "%s|securemode=2,signmethod=hmacsha256|", EXAMPLE_ALIYUN_DK);
    sprintf(username, "%s&%s", EXAMPLE_ALIYUN_DK, EXAMPLE_ALIYUN_PK);
    for (i = 0; i < sizeof(hmac); i++) {
        sprintf(password + (2 * i), "%02X", hmac[i]);
    }
}

static void report_version() {
    const esp_partition_t *part = NULL;
    esp_app_desc_t app_desc = {0};
    char buf[256] = {0};

    part = esp_ota_get_running_partition();
    esp_partition_read(part, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), &app_desc, sizeof(esp_app_desc_t));
    sprintf(buf, "{\"id\":\"%s\", \"params\":{\"version\":\"%s\"}}", gen_post_msg_id(), app_desc.version);
    esp_mqtt_client_publish(hd_mqtt, EXAMPLE_MQTT_TOPIC_OTA_VERSION, buf, strlen(buf), 0, 0);
    ESP_LOGI(TAG, "report version: %s", app_desc.version);
}

static void report_progress(uint8_t progress) {
    char buf[256] = {0};

    sprintf(buf, "{\"id\":\"%s\", \"params\":{\"step\":\"%u\", \"desc\":\"success\"}}", gen_post_msg_id(), progress);
    esp_mqtt_client_publish(hd_mqtt, EXAMPLE_MQTT_TOPIC_OTA_PROGRESS, buf, strlen(buf), 0, 0);
    ESP_LOGI(TAG, "report progress: %u%%", progress);
}

static void set_tsl(char *payload, uint32_t len) {
    cJSON *root = cJSON_Parse(payload);
    cJSON *param_json = cJSON_GetObjectItem(root, "params");
    cJSON *rmtId_json = cJSON_GetObjectItem(param_json, "rmtId");
    cJSON *channelId_json = cJSON_GetObjectItem(param_json, "channelId");
    uint8_t has_channelId = 0;

    if (rmtId_json) {
        rmt_id = rmtId_json->valueint;
    }
    if (channelId_json) {
        channel_id = channelId_json->valueint;
        has_channelId = 1;
    }
    cJSON_Delete(root);

    if (!has_channelId) {
        ESP_LOGI(TAG, "rmt_id:%u", rmt_id);
    } else {
        ESP_LOGI(TAG, "rmt_id:%u, channel_id:%u", rmt_id, channel_id);
        ir_rmt_recv(rmt_id, channel_id);
    }
}

static void report_progress_cb(void *pvParameters) {
    uint8_t progress = 0;
    static uint8_t last_progress = 0;
    
    while (1) {
        progress = ota_task.download_size * 100 / ota_task.size;
        if (last_progress != progress) {
            report_progress(progress);
            last_progress = progress;        
        }

        vTaskDelay(EXAMPLE_TIMER_PERIOD_REPORT_PROGRESS / portTICK_PERIOD_MS);
    }
}

static void download_image_cb(void *pvParameters) {
    esp_err_t err = ESP_OK;
    const esp_partition_t *part = NULL;
    int len = 0;
    esp_app_desc_t app_desc = {0};
    uint8_t retry = 0;
    char buf[1024] = {0};
    uint8_t sha256[32] = {0};
    char sha256_string[65] = {0};
    mbedtls_md_context_t md_ctx;
    esp_http_client_config_t http_cfg = {
        .url = ota_task.url,
        .method = HTTP_METHOD_GET,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = (char *)global_sign_crt_start,
        // .crt_bundle_attach = esp_crt_bundle_attach,
    };

    part = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "update partition, address:0x%08"PRIx32" size:0x%08"PRIx32"", part->address, part->size);

    for (retry = 0; retry < 3; retry ++) {
        ESP_LOGI(TAG, "download image retry: %u", retry);

        ota_task.download_size = 0;
        mbedtls_md_init(&md_ctx);  
        mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);  
        mbedtls_md_starts(&md_ctx);
        err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &hd_ota);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "esp_ota_begin failed:%s", esp_err_to_name(err));
            goto exit;
        }
        ESP_LOGI(TAG, "esp_ota_begin ok");

        hd_http = esp_http_client_init(&http_cfg);
        err = esp_http_client_open(hd_http, 0);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "open http connection failed:%s", esp_err_to_name(err));
            goto exit;
        }

        esp_http_client_fetch_headers(hd_http);
        ESP_LOGI(TAG, "Content-Length: %lld", esp_http_client_get_content_length(hd_http));
        
        while (1) {
            len = esp_http_client_read(hd_http, buf, 1024);
            if (len < 0) {
                ESP_LOGE(TAG, "read http data failed:%d", len);
                goto exit;
            } else if (len > 0) {
                err = esp_ota_write(hd_ota, buf, len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed:%s", esp_err_to_name(err));
                    goto exit;
                }
                mbedtls_md_update(&md_ctx, (unsigned char *)buf, len);
                ota_task.download_size += len;
                ESP_LOGI(TAG, "esp_ota_write ok, cur:%d totol:%lu", len, ota_task.download_size);
                vTaskDelay(100 / portTICK_PERIOD_MS);
            } else if (len == 0) {
                ESP_LOGW(TAG, "connection closed");
                if (esp_http_client_is_complete_data_received(hd_http)) {
                    ESP_LOGI(TAG, "all data received ok");
                    break;
                } else {
                    ESP_LOGE(TAG, "all data received failed");
                    goto exit;
                }
            }
        }

        if (ota_task.size != ota_task.download_size) {
            ESP_LOGE(TAG, "size check failed, %lu:%lu", ota_task.size, ota_task.download_size);
            goto exit;   
        }
        esp_partition_read(part, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), &app_desc, sizeof(esp_app_desc_t));
        if (memcmp(ota_task.version, app_desc.version, strlen(app_desc.version))) {
            ESP_LOGE(TAG, "version check failed, %s:%s", ota_task.version, app_desc.version);
            goto exit;   
        }
        mbedtls_md_finish(&md_ctx, sha256);
        for (uint8_t i = 0; i < sizeof(sha256); i++) {
            sprintf(sha256_string + (2 * i), "%02x", sha256[i]);
        }
        if (memcmp(ota_task.sha256, sha256_string, strlen(sha256_string))) {
            ESP_LOGE(TAG, "sha256 check failed, %s:%s", ota_task.sha256, sha256_string);
            goto exit;   
        }
        ESP_LOGI(TAG, "download image check ok");

        err = esp_ota_end(hd_ota);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "esp_ota_end failed:%s", esp_err_to_name(err));
            goto exit;
        }
        ESP_LOGI(TAG, "esp_ota_end ok");

        err = esp_ota_set_boot_partition(part);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed:%s", esp_err_to_name(err));
            goto exit;
        }
        ESP_LOGI(TAG, "esp_ota_set_boot_partition ok");

        ESP_LOGI(TAG, "restart after 3s");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        esp_restart();

exit:
        mbedtls_md_free(&md_ctx);
        esp_http_client_cleanup(hd_http);
        esp_ota_abort(hd_ota);
    }
    
    vTaskDelete(NULL);
}

static void start_ota(char *payload, uint32_t len) {
    cJSON *root = cJSON_Parse(payload);
    cJSON *data_json = cJSON_GetObjectItem(root, "data");
    cJSON *size_json = cJSON_GetObjectItem(data_json, "size");
    cJSON *version_json = cJSON_GetObjectItem(data_json, "version");
    cJSON *url_json = cJSON_GetObjectItem(data_json, "url");
    cJSON *sha256_json = cJSON_GetObjectItem(data_json, "sign");

    memset(&ota_task, 0, sizeof(ota_task));
    memcpy(ota_task.url, url_json->valuestring, strlen(url_json->valuestring));
    memcpy(ota_task.version, version_json->valuestring, strlen(version_json->valuestring));
    memcpy(ota_task.sha256, sha256_json->valuestring, strlen(sha256_json->valuestring));
    ota_task.size = size_json->valueint;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "url:%s", ota_task.url);
    ESP_LOGI(TAG, "version:%s", ota_task.version);
    ESP_LOGI(TAG, "sha256:%s", ota_task.sha256);
    ESP_LOGI(TAG, "size:%lu", ota_task.size);

    xTaskCreate(download_image_cb, "download_image", 8192, NULL, 3, NULL);
    xTaskCreate(report_progress_cb, "report_progress", 4096, NULL, 2, NULL);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        subscribe_id[0] = esp_mqtt_client_subscribe(hd_mqtt, EXAMPLE_MQTT_TOPIC_SET, 0);
        subscribe_id[1] = esp_mqtt_client_subscribe(hd_mqtt, EXAMPLE_MQTT_TOPIC_OTA_TASK, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGE(TAG, "MQTT_EVENT_DISCONNECTED");
        network_stat = 1;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (event->msg_id == subscribe_id[0]) {
            ESP_LOGI(TAG, "subscribe ok:%s", EXAMPLE_MQTT_TOPIC_SET);
        } else if (event->msg_id == subscribe_id[1]) {
            ESP_LOGI(TAG, "subscribe ok:%s", EXAMPLE_MQTT_TOPIC_OTA_TASK);
            report_version();
            network_stat = 2;
        }
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGE(TAG, "MQTT_EVENT_PUBLISHED");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        if (0 == strncmp(event->topic, EXAMPLE_MQTT_TOPIC_SET, strlen(EXAMPLE_MQTT_TOPIC_SET))) {
            set_tsl(event->data, event->data_len);
        } else if (0 == strncmp(event->topic, EXAMPLE_MQTT_TOPIC_OTA_TASK, strlen(EXAMPLE_MQTT_TOPIC_OTA_TASK))) {
            start_ota(event->data, event->data_len);
        }
        break;
    default:
        ESP_LOGW(TAG, "unknown MQTT_EVENT:%d", event->event_id);
        break;
    }
}

static void mqtt_connect_cloud() {
    char client_id[128] = {0};
    char username[128] = {0};
    char password[128] = {0};

    gen_mqtt_credential(client_id, username, password);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = EXAMPLE_MQTT_URL,
        .broker.address.port = EXAMPLE_MQTT_PORT,
        .broker.verification.certificate = (char *)global_sign_crt_start,
        .credentials.client_id = client_id,
        .credentials.username = username,
        .credentials.authentication.password = password,
        .session.keepalive = EXAMPLE_MQTT_KEEP_ALIVE,
        // .credentials.authentication.certificate = client_crt_start,
        // .credentials.authentication.key = client_priv_key_start,
    };

    hd_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(hd_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(hd_mqtt);
}

static esp_err_t http_get_index_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

static esp_err_t http_cfg_wifi_handler(httpd_req_t *req) {
    char post_data[256] = {0};
    cJSON *root = NULL, *ssid_json = NULL, *pwd_json = NULL;
    wifi_config_t sta_cfg = {0};

    httpd_req_recv(req, post_data, sizeof(post_data));
    ESP_LOGI(TAG, "http post data:%s", post_data);
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, "success", strlen("success"));

    root = cJSON_Parse(post_data);
    ssid_json = cJSON_GetObjectItem(root, "ssid");
    pwd_json = cJSON_GetObjectItem(root, "pwd");
    memcpy(sta_ssid, ssid_json->valuestring, strlen(ssid_json->valuestring));
    memcpy(sta_pwd, pwd_json->valuestring, strlen(pwd_json->valuestring));
    sta_ssid_len = strlen(ssid_json->valuestring);
    sta_pwd_len = strlen(pwd_json->valuestring);
    cJSON_Delete(root);

    save_cfg_to_flash();

    esp_wifi_disconnect();
    memcpy(sta_cfg.sta.ssid, sta_ssid, sta_ssid_len);
    memcpy(sta_cfg.sta.password, sta_pwd, sta_pwd_len);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();

    return ESP_OK;
}

static void http_start_server() {
    const httpd_uri_t uri_get_index = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = http_get_index_handler,
        .user_ctx  = NULL,
    };

    const httpd_uri_t uri_cfg_wifi = {
        .uri       = "/cfg_wifi",
        .method    = HTTP_POST,
        .handler   = http_cfg_wifi_handler,
        .user_ctx  = NULL,
    };
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&hd_httpd, &httpd_cfg) == ESP_OK) {
        ESP_LOGI(TAG, "start http server ok, port:%d", httpd_cfg.server_port);
        httpd_register_uri_handler(hd_httpd, &uri_get_index);
        httpd_register_uri_handler(hd_httpd, &uri_cfg_wifi);
    } else {
        ESP_LOGE(TAG, "start http server failed");        
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    wifi_event_sta_connected_t *sta_conn_event = NULL;
    wifi_event_sta_disconnected_t *sta_disconn_event = NULL;
    wifi_event_ap_staconnected_t *ap_sta_conn_event = NULL;
    wifi_event_ap_stadisconnected_t *ap_sta_disconn_event = NULL;
    ip_event_got_ip_t *got_ip_event = NULL;

    if (WIFI_EVENT == event_base) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "wifi_sta start connect, %s:%s", sta_ssid, sta_pwd);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            sta_conn_event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "wifi_sta connected, channel:%u authmode:%u", sta_conn_event->channel, sta_conn_event->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            sta_disconn_event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "wifi_sta disconnected, reason:%u", sta_disconn_event->reason);
            network_stat = 0;
            esp_wifi_connect();
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ap_sta_conn_event = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "wifi_ap station join, AID:%u", ap_sta_conn_event->aid);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ap_sta_disconn_event = (wifi_event_ap_stadisconnected_t*)event_data;
            ESP_LOGI(TAG, "wifi_ap station leave, AID:%u", ap_sta_disconn_event->aid);
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
            ESP_LOGI(TAG, "wifi_sta got ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_ip_event->ip_info.ip), IP2STR(&got_ip_event->ip_info.netmask), IP2STR(&got_ip_event->ip_info.gw));
            network_stat = 1;
            mqtt_connect_cloud();
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGE(TAG, "wifi_sta lost ip");
            break;
        default:
            ESP_LOGW(TAG, "unknown IP_EVENT:%ld", event_id);
            break;   
        }
    }
}

static void led_task(void* parameter) {	
    gpio_config_t led_conf = {0};

    led_conf.intr_type = GPIO_INTR_DISABLE;
    led_conf.pin_bit_mask = 1ULL << EXAMPLE_LED_GPIO_NUM;
    led_conf.mode = GPIO_MODE_OUTPUT;
    led_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    led_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&led_conf);

    gpio_set_level(EXAMPLE_LED_GPIO_NUM, 0); // 0 - off, 1 - on

    while (1) {
        if (0 == network_stat) {
            gpio_set_level(EXAMPLE_LED_GPIO_NUM, 1);
            vTaskDelay(500/portTICK_PERIOD_MS);
            gpio_set_level(EXAMPLE_LED_GPIO_NUM, 0);
            vTaskDelay(500/portTICK_PERIOD_MS);
        } else if (1 == network_stat) {
            gpio_set_level(EXAMPLE_LED_GPIO_NUM, 1);
            vTaskDelay(1000/portTICK_PERIOD_MS);
            gpio_set_level(EXAMPLE_LED_GPIO_NUM, 0);
            vTaskDelay(1000/portTICK_PERIOD_MS);            
        } else if (2 == network_stat) {
            gpio_set_level(EXAMPLE_LED_GPIO_NUM, 1);
            vTaskDelay(1000/portTICK_PERIOD_MS);       
        }
    }
}

void app_main(void) {
    esp_err_t err = ESP_OK;
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t sta_cfg = {0};
    wifi_config_t ap_cfg = {0};
    uint8_t sta_mac[6] = {0};
    esp_netif_t *ap_netif = NULL;
    esp_netif_ip_info_t ap_netif_ip = {0};
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
    ap_netif = esp_netif_create_default_wifi_ap();
    esp_wifi_init(&init_cfg);

    read_cfg_from_flash();
    if (sta_ssid_len) {
        memcpy(sta_cfg.sta.ssid, sta_ssid, sta_ssid_len);
    }
    if (sta_pwd_len) {
        memcpy(sta_cfg.sta.password, sta_pwd, sta_pwd_len);
    }
    ESP_LOGI(TAG, "wifi station, ssid:%s pwd:%s", sta_ssid, sta_pwd);

    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
    sprintf((char *)ap_cfg.ap.ssid, "ESP32_%02X%02X", sta_mac[4], sta_mac[5]);
    ap_cfg.ap.ssid_len = strlen((char *)ap_cfg.ap.ssid);
    ap_cfg.ap.channel = EXAMPLE_WIFI_AP_CHANNEL;
    ap_cfg.ap.max_connection = EXAMPLE_WIFI_AP_MAX_CONN;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    ap_cfg.ap.ssid_hidden = 0;
    ESP_LOGI(TAG, "wifi ap, ssid:%s", ap_cfg.ap.ssid);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_cfg);
    esp_wifi_start();

    esp_netif_dhcps_stop(ap_netif);
    ap_netif_ip.ip.addr = esp_ip4addr_aton(EXAMPLE_WIFI_AP_IP);
    ap_netif_ip.netmask.addr = esp_ip4addr_aton(EXAMPLE_WIFI_AP_NETMASK);
    ap_netif_ip.gw.addr = esp_ip4addr_aton(EXAMPLE_WIFI_AP_IP);
    esp_netif_set_ip_info(ap_netif, &ap_netif_ip);
    esp_netif_dhcps_start(ap_netif);

    http_start_server();

    ir_rmt_init();

    xTaskCreate(led_task, "led_task", 1024, NULL, 1, NULL);

    while (1) {
        ESP_LOGI(TAG, "%lu", i++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
