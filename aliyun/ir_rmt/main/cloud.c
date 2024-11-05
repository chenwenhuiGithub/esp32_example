#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "mqtt_client.h"
#include "mbedtls/md.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "cJSON.h"
#include "netcfg.h"
#include "tsl.h"
#include "cloud.h"


typedef struct {
    char url[256];
    char version[16];
    char sha256[128];
    uint32_t size;
    uint32_t download_size;
} cloud_ota_task_t;

extern const uint8_t global_sign_crt_start[]            asm("_binary_global_sign_crt_start");
extern const uint8_t global_sign_crt_end[]              asm("_binary_global_sign_crt_end");

static cloud_ota_task_t s_ota_task = {0};
static esp_ota_handle_t s_hd_ota = 0;
static esp_http_client_handle_t s_hd_http = NULL;
static esp_mqtt_client_handle_t s_hd_mqtt = NULL;
static int s_subscribe_id[2] = {0};
static const char *TAG = "cloud";

char *cloud_gen_msg_id() {
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

static void calc_mqtt_credential(char *client_id, char *username, char *password) {
    char buf[256] = {0};
    uint8_t hmac[32] = {0};
    mbedtls_md_context_t md_ctx;
    uint32_t i = 0;

    sprintf(buf, "clientId%sdeviceName%sproductKey%s", CONFIG_CLOUD_DK, CONFIG_CLOUD_DK, CONFIG_CLOUD_PK);

    mbedtls_md_init(&md_ctx);
    mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md_ctx, (unsigned char *)CONFIG_CLOUD_DS, strlen(CONFIG_CLOUD_DS));
    mbedtls_md_hmac_update(&md_ctx, (unsigned char *)buf, strlen(buf));
    mbedtls_md_hmac_finish(&md_ctx, hmac);
    mbedtls_md_free(&md_ctx);

    sprintf(client_id, "%s|securemode=2,signmethod=hmacsha256|", CONFIG_CLOUD_DK);
    sprintf(username, "%s&%s", CONFIG_CLOUD_DK, CONFIG_CLOUD_PK);
    for (i = 0; i < sizeof(hmac); i++) {
        sprintf(password + (2 * i), "%02X", hmac[i]);
    }
}

static void ota_report_progress_task(void *pvParameters) {
    uint8_t progress = 0;
    static uint8_t last_progress = 0;
    char buf[256] = {0};
    
    while (1) {
        progress = s_ota_task.download_size * 100 / s_ota_task.size;
        if (last_progress != progress) {
            sprintf(buf, "{\"id\":\"%s\", \"params\":{\"step\":\"%u\", \"desc\":\"success\"}}", cloud_gen_msg_id(), progress);
            esp_mqtt_client_publish(s_hd_mqtt, CONFIG_TOPIC_OTA_REPORT_PROGRESS, buf, strlen(buf), 0, 0);
            ESP_LOGI(TAG, "report progress: %u%%", progress);
            last_progress = progress;        
        }
        vTaskDelay(CONFIG_OTA_PERIOD_REPORT_PROGRESS / portTICK_PERIOD_MS);
    }
}

static void ota_download_image_task(void *pvParameters) {
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
        .url = s_ota_task.url,
        .method = HTTP_METHOD_GET,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = (char *)global_sign_crt_start,
    };

    part = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "ota partition, address:0x%08"PRIx32" size:0x%08"PRIx32"", part->address, part->size);

    for (retry = 0; retry < 3; retry ++) {
        ESP_LOGI(TAG, "download image retry:%u", retry);

        s_ota_task.download_size = 0;
        mbedtls_md_init(&md_ctx);  
        mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);  
        mbedtls_md_starts(&md_ctx);
        err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &s_hd_ota);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "esp_ota_begin failed:%s", esp_err_to_name(err));
            goto exit;
        }
        ESP_LOGI(TAG, "esp_ota_begin ok");

        s_hd_http = esp_http_client_init(&http_cfg);
        err = esp_http_client_open(s_hd_http, 0);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "open http connection failed:%s", esp_err_to_name(err));
            goto exit;
        }

        esp_http_client_fetch_headers(s_hd_http);
        ESP_LOGI(TAG, "Content-Length: %lld", esp_http_client_get_content_length(s_hd_http));
        
        while (1) {
            len = esp_http_client_read(s_hd_http, buf, 1024);
            if (len < 0) {
                ESP_LOGE(TAG, "read http data failed:%d", len);
                goto exit;
            } else if (len > 0) {
                err = esp_ota_write(s_hd_ota, buf, len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed:%s", esp_err_to_name(err));
                    goto exit;
                }
                mbedtls_md_update(&md_ctx, (unsigned char *)buf, len);
                s_ota_task.download_size += len;
                ESP_LOGI(TAG, "esp_ota_write ok, cur:%d totol:%lu", len, s_ota_task.download_size);
                vTaskDelay(100 / portTICK_PERIOD_MS);
            } else if (len == 0) {
                ESP_LOGW(TAG, "connection closed");
                if (esp_http_client_is_complete_data_received(s_hd_http)) {
                    ESP_LOGI(TAG, "all data received ok");
                    break;
                } else {
                    ESP_LOGE(TAG, "all data received failed");
                    goto exit;
                }
            }
        }

        if (s_ota_task.size != s_ota_task.download_size) {
            ESP_LOGE(TAG, "size check failed, %lu:%lu", s_ota_task.size, s_ota_task.download_size);
            goto exit;
        }
        esp_partition_read(part, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), &app_desc, sizeof(esp_app_desc_t));
        if (memcmp(s_ota_task.version, app_desc.version, strlen(app_desc.version))) {
            ESP_LOGE(TAG, "version check failed, %s:%s", s_ota_task.version, app_desc.version);
            goto exit;   
        }
        mbedtls_md_finish(&md_ctx, sha256);
        for (uint8_t i = 0; i < sizeof(sha256); i++) {
            sprintf(sha256_string + (2 * i), "%02x", sha256[i]);
        }
        if (memcmp(s_ota_task.sha256, sha256_string, strlen(sha256_string))) {
            ESP_LOGE(TAG, "sha256 check failed, %s:%s", s_ota_task.sha256, sha256_string);
            goto exit;   
        }
        ESP_LOGI(TAG, "download image check ok");

        err = esp_ota_end(s_hd_ota);
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
        esp_http_client_cleanup(s_hd_http);
        esp_ota_abort(s_hd_ota);
    }

    vTaskDelete(NULL);
}

static void ota_start(uint8_t *payload, uint32_t len) {
    cJSON *root = cJSON_Parse((char *)payload);
    cJSON *data_json = cJSON_GetObjectItem(root, "data");
    cJSON *size_json = cJSON_GetObjectItem(data_json, "size");
    cJSON *version_json = cJSON_GetObjectItem(data_json, "version");
    cJSON *url_json = cJSON_GetObjectItem(data_json, "url");
    cJSON *sha256_json = cJSON_GetObjectItem(data_json, "sign");

    memset(&s_ota_task, 0, sizeof(s_ota_task));
    memcpy(s_ota_task.url, url_json->valuestring, strlen(url_json->valuestring));
    memcpy(s_ota_task.version, version_json->valuestring, strlen(version_json->valuestring));
    memcpy(s_ota_task.sha256, sha256_json->valuestring, strlen(sha256_json->valuestring));
    s_ota_task.size = size_json->valueint;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "url:%s", s_ota_task.url);
    ESP_LOGI(TAG, "version:%s", s_ota_task.version);
    ESP_LOGI(TAG, "sha256:%s", s_ota_task.sha256);
    ESP_LOGI(TAG, "size:%lu", s_ota_task.size);

    xTaskCreate(ota_download_image_task, "ota_download_image_task", 8192, NULL, 3, NULL);
    xTaskCreate(ota_report_progress_task, "ota_report_progress_task", 4096, NULL, 2, NULL);
}

static void ota_report_version() {
    const esp_partition_t *part = NULL;
    esp_app_desc_t app_desc = {0};
    char buf[256] = {0};

    part = esp_ota_get_running_partition();
    esp_partition_read(part, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), &app_desc, sizeof(esp_app_desc_t));
    sprintf(buf, "{\"id\":\"%s\", \"params\":{\"version\":\"%s\"}}", cloud_gen_msg_id(), app_desc.version);
    esp_mqtt_client_publish(s_hd_mqtt, CONFIG_TOPIC_OTA_REPORT_VERSION, buf, strlen(buf), 0, 0);
    ESP_LOGI(TAG, "ota report version:%s", app_desc.version);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        s_subscribe_id[0] = esp_mqtt_client_subscribe(s_hd_mqtt, CONFIG_TOPIC_TSL_SET, 0);
        s_subscribe_id[1] = esp_mqtt_client_subscribe(s_hd_mqtt, CONFIG_TOPIC_OTA_UPGRADE_TASK, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGE(TAG, "MQTT_EVENT_DISCONNECTED");
        netcfg_set_netstat(NETSTAT_WIFI_CONNECTED);
        cloud_start_connect();
        ESP_LOGI(TAG, "mqtt start connect");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (event->msg_id == s_subscribe_id[0]) {
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, topic:%s", CONFIG_TOPIC_TSL_SET);
        } else if (event->msg_id == s_subscribe_id[1]) {
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, topic:%s", CONFIG_TOPIC_OTA_UPGRADE_TASK);
            netcfg_set_netstat(NETSTAT_CLOUD_CONNECTED);
            ota_report_version();
        }
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "topic:%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "payload:%.*s", event->data_len, event->data);
        if (0 == strncmp(event->topic, CONFIG_TOPIC_TSL_SET, strlen(CONFIG_TOPIC_TSL_SET))) {
            tsl_recv_set((uint8_t *)event->data, event->data_len);
        } else if (0 == strncmp(event->topic, CONFIG_TOPIC_OTA_UPGRADE_TASK, strlen(CONFIG_TOPIC_OTA_UPGRADE_TASK))) {
            ota_start((uint8_t *)event->data, event->data_len);
        } else {
            ESP_LOGW(TAG, "unknown topic:%.*s", event->topic_len, event->topic);
        }
        break;
    default:
        ESP_LOGW(TAG, "unknown MQTT_EVENT:%d", event->event_id);
        break;
    }
}

void cloud_start_connect() {
    esp_err_t ret = ESP_OK;
    char client_id[128] = {0};
    char username[128] = {0};
    char password[128] = {0};

    calc_mqtt_credential(client_id, username, password);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_URL,
        .broker.address.port = CONFIG_MQTT_PORT,
        .broker.verification.certificate = (char *)global_sign_crt_start,
        .credentials.client_id = client_id,
        .credentials.username = username,
        .credentials.authentication.password = password,
        .session.keepalive = CONFIG_MQTT_KEEP_ALIVE,
        // .credentials.authentication.certificate = client_crt_start,
        // .credentials.authentication.key = client_priv_key_start,
        .task.stack_size = 4096, // default:6144
    };

    s_hd_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_hd_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ret = esp_mqtt_client_start(s_hd_mqtt);
    if (ESP_OK != ret) {
        ESP_LOGI(TAG, "mqtt client start failed:%d", ret);
    } else {
        ESP_LOGI(TAG, "mqtt client start ok");
    }
}

void cloud_stop_connect() {
    if (s_hd_mqtt) {
        esp_mqtt_client_stop(s_hd_mqtt);
    }
}

void cloud_send_publish(char *topic, uint8_t *payload, uint32_t len) {
    if (s_hd_mqtt) {
        esp_mqtt_client_publish(s_hd_mqtt, topic, (char *)payload, len, 0, 0);
    }
}
