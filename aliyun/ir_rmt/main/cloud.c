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
#include "ota.h"


extern const uint8_t remote_server_root_crt_start[]            asm("_binary_remote_server_root_crt_start");
extern const uint8_t remote_server_root_crt_end[]              asm("_binary_remote_server_root_crt_end");

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
        .broker.verification.certificate = (char *)remote_server_root_crt_start,
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
