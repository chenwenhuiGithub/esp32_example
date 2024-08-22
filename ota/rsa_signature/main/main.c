#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_flash_partitions.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "mbedtls/platform.h"
#include "mbedtls/pk.h" 
#include "mbedtls/md.h"


#define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_IMAGE_FILE_URL                      "https://solax-public.oss-cn-hangzhou.aliyuncs.com/bin/https_ota_sign.bin"
#define BUF_LEN                                     1024
#define SHA256_LEN                                  32
#define SIGNATURE_LEN                               256  // rsa 数字签名私钥长度 2048 bits

extern const char server_root_crt_start[]           asm("_binary_server_root_crt_start");
extern const char server_root_crt_end[]             asm("_binary_server_root_crt_end");
extern const char sign_pub_key_start[]              asm("_binary_sign_pub_key_start");
extern const char sign_pub_key_end[]                asm("_binary_sign_pub_key_end");

static char buf[BUF_LEN] = {0};
static const char *TAG = "https_ota";

static int verify_signature(const esp_partition_t *update_partition, int64_t content_len)
{
    int ret = 0, i = 0;
    mbedtls_pk_context pk;
    mbedtls_md_context_t ctx;
    uint8_t hash_data[SHA256_LEN] = {0};
    uint8_t sign_data[SIGNATURE_LEN] = {0};
    uint32_t image_size = content_len - SIGNATURE_LEN;

    mbedtls_pk_init(&pk);

    // 读取公钥
    ret = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)sign_pub_key_start, sign_pub_key_end - sign_pub_key_start);
    if (ret) {
        ESP_LOGE(TAG, "mbedtls_pk_parse_public_key failed:%d", ret);
        mbedtls_pk_free(&pk);
        return ret;
    }

    // 计算应用程序镜像 hash
    mbedtls_md_init(&ctx);  
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);  
    mbedtls_md_starts(&ctx); 
    while (i < image_size / BUF_LEN) {
        esp_partition_read(update_partition, i * BUF_LEN, buf, BUF_LEN);
        mbedtls_md_update(&ctx, (unsigned char *)buf, BUF_LEN);
        i++;
    }
    if (image_size % BUF_LEN) {
        esp_partition_read(update_partition, i * BUF_LEN, buf, image_size % BUF_LEN);
        mbedtls_md_update(&ctx, (unsigned char *)buf, image_size % BUF_LEN);
    }
    mbedtls_md_finish(&ctx, hash_data);
    mbedtls_md_free(&ctx);  
    ESP_LOGI(TAG, "hash_data");
    ESP_LOG_BUFFER_HEX(TAG, hash_data, SHA256_LEN);

    // 读取数字签名
    esp_partition_read(update_partition, image_size, sign_data, SIGNATURE_LEN);
    ESP_LOGI(TAG, "sign_data");
    ESP_LOG_BUFFER_HEX(TAG, sign_data, SIGNATURE_LEN);

    // 验证数字签名
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash_data, SHA256_LEN, sign_data, SIGNATURE_LEN);
    if (ret) {
        ESP_LOGE(TAG, "mbedtls_pk_verify failed:%d", ret);
    }
    mbedtls_pk_free(&pk);

    return ret;
}

static int verify_version(const esp_partition_t *update_partition)
{
    int ret = 0;
    const esp_partition_t *running_partition = NULL;
    esp_app_desc_t running_app_desc = {0};
    esp_app_desc_t ota_app_desc = {0};

    running_partition = esp_ota_get_running_partition();
    esp_ota_get_partition_description(running_partition, &running_app_desc);
    esp_partition_read(update_partition, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), &ota_app_desc, sizeof(esp_app_desc_t));
    ESP_LOGI(TAG, "firmware_version, cur:%s ota:%s", running_app_desc.version, ota_app_desc.version);
    if (memcmp(ota_app_desc.version, running_app_desc.version, strlen(ota_app_desc.version)) == 0) {
        ESP_LOGE(TAG, "firmware_version are same");
        return 1;
    }

    return ret;
}

static void ota_task(void *pvParameters)
{
    esp_err_t err = ESP_OK;
    const esp_partition_t *update_partition = NULL;
    bool image_header_checked = false;
    int cur_data_len = 0;
    int total_data_len = 0;
    esp_ota_handle_t update_handle = 0;
    char *content_type = NULL;
    int64_t content_len = 0;
    
    esp_http_client_config_t config = {
        .url = EXAMPLE_IMAGE_FILE_URL,
        .method = HTTP_METHOD_GET,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        // .cert_pem = server_root_crt_start,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    ESP_LOGI(TAG, "start OTA task");

    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "update partition, address:0x%08"PRIx32" size:0x%08"PRIx32"",
             update_partition->address, update_partition->size);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    err = esp_http_client_open(client, 0);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "open http connection failed:%s", esp_err_to_name(err));
        goto exit;
    }

    esp_http_client_fetch_headers(client);
    esp_http_client_get_header(client, "Content-Type", &content_type);
    if (content_type) {
        ESP_LOGI(TAG, "Content-Type: %s", content_type);
    }
    if (esp_http_client_is_chunked_response(client)) {
        ESP_LOGI(TAG, "Transfer-Encoding: chunked");
    } else {
        content_len = esp_http_client_get_content_length(client); // ota_file(Content-Length) = image + signature(256B)
        ESP_LOGI(TAG, "Content-Length: %lld", content_len);
    }

    while (1) {
        cur_data_len = esp_http_client_read(client, buf, BUF_LEN);
        if (cur_data_len < 0) {
            ESP_LOGE(TAG, "read http data failed:%d", cur_data_len);
            goto exit;
        } else if (cur_data_len > 0) {
            if (image_header_checked == false) {
                err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                if (ESP_OK != err) {
                    ESP_LOGE(TAG, "esp_ota_begin failed:%s", esp_err_to_name(err));
                    goto exit;
                }
                image_header_checked = true;
                ESP_LOGI(TAG, "esp_ota_begin ok");
            }

            err = esp_ota_write(update_handle, buf, cur_data_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed:%s", esp_err_to_name(err));
                goto exit;
            }
            total_data_len += cur_data_len;
            ESP_LOGI(TAG, "esp_ota_write, cur:%d total:%d image:%lld", cur_data_len, total_data_len, content_len);
        } else if (cur_data_len == 0) {
            ESP_LOGW(TAG, "connection closed");
            if (esp_http_client_is_complete_data_received(client)) {
                ESP_LOGI(TAG, "all data received ok");
                break;
            } else {
                ESP_LOGE(TAG, "all data received failed");
                goto exit;
            }
        }
    }

    // 数字签名验证
    if (verify_signature(update_partition, content_len)) {
        ESP_LOGE(TAG, "verify signature failed");
        goto exit;
    }

    // 版本号校验
    if (verify_version(update_partition)) {
        ESP_LOGE(TAG, "verify version failed");
        goto exit;
    }

    err = esp_ota_end(update_handle);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "esp_ota_end failed:%s", esp_err_to_name(err));
        goto exit;
    }
    ESP_LOGI(TAG, "esp_ota_end ok");

    err = esp_ota_set_boot_partition(update_partition);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed:%s", esp_err_to_name(err));
        goto exit;
    }
    ESP_LOGI(TAG, "esp_ota_set_boot_partition ok");

    ESP_LOGI(TAG, "restart after 3s");
    vTaskDelay(1000/portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "restart after 2s");
    vTaskDelay(1000/portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "restart after 1s");
    vTaskDelay(1000/portTICK_PERIOD_MS);
    esp_restart();

exit:
    esp_http_client_cleanup(client);
    esp_ota_abort(update_handle);
    vTaskDelete(NULL);
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
            xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL); // StackType_t:uint8_t
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
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}
