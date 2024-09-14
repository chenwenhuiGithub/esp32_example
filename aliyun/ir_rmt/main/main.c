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
#include "ir_rmt.h"


#define EXAMPLE_WIFI_SSID                       "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                        "15958187121"
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
#define EXAMPLE_TIMER_PERIOD_SAVE_TSL           10000 // 10s
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

static esp_mqtt_client_handle_t hd_mqtt = NULL;
static esp_ota_handle_t hd_ota = 0;
static esp_http_client_handle_t hd_http = NULL;
static int subscribe_id[2] = {0};
static ota_task_info_t ota_task = {0};

static uint8_t channel_id = 0;
static uint8_t rmt_id = 0;


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
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (event->msg_id == subscribe_id[0]) {
            ESP_LOGI(TAG, "subscribe ok:%s", EXAMPLE_MQTT_TOPIC_SET);
        } else if (event->msg_id == subscribe_id[1]) {
            ESP_LOGI(TAG, "subscribe ok:%s", EXAMPLE_MQTT_TOPIC_OTA_TASK);
            report_version();
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
            mqtt_connect_cloud();
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

    ir_rmt_init();

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