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


#define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_HTTP_RESPONSE_BUF_SIZE              1024
#define EXAMPLE_USE_HTTPS                           1


#if EXAMPLE_USE_HTTPS == 1
#define EXAMPLE_IMAGE_FILE_URL                      "https://solax-public.oss-cn-hangzhou.aliyuncs.com/bin/image_ota.bin"

extern const char server_cert_pem_start[]           asm("_binary_server_cert_pem_start");
extern const char server_cert_pem_end[]             asm("_binary_server_cert_pem_end");
#else
#define EXAMPLE_IMAGE_FILE_URL                      "http://solax-public.oss-cn-hangzhou.aliyuncs.com/bin/image_ota.bin"
#endif


static char buf[EXAMPLE_HTTP_RESPONSE_BUF_SIZE] = { 0 };
static const char *TAG = "image_base";

static void ota_task(void *pvParameters)
{
    esp_err_t err = ESP_OK;
    const esp_partition_t *running_partition = NULL;
    const esp_partition_t *update_partition = NULL;
    uint8_t sha256[32] = {0};
    bool image_header_checked = false;
    int cur_data_len = 0;
    int total_data_len = 0;
    esp_app_desc_t running_app_desc = {0};
    esp_app_desc_t update_app_desc = {0};
    esp_ota_handle_t update_handle = 0;
    char *content_type = NULL;
    
    esp_http_client_config_t config = {
        .url = EXAMPLE_IMAGE_FILE_URL,
        .method = HTTP_METHOD_GET,
#if EXAMPLE_USE_HTTPS == 1
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        // .cert_pem = server_cert_pem_start,
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
#endif
        .timeout_ms = 5000,
    };

    ESP_LOGI(TAG, "starting OTA task");

    running_partition = esp_ota_get_running_partition();
    esp_partition_get_sha256(running_partition, sha256);
    esp_ota_get_partition_description(running_partition, &running_app_desc);
    ESP_LOGI(TAG, "running partition, address:0x%08"PRIx32" size:0x%08"PRIx32"", running_partition->address, running_partition->size);
    ESP_LOGI(TAG, "running firmware_version:%s", running_app_desc.version);
    ESP_LOG_BUFFER_HEX(TAG, sha256, sizeof(sha256));

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
        ESP_LOGI(TAG, "Content-Length: %lld", esp_http_client_get_content_length(client));
    }
    
    while (1) {
        cur_data_len = esp_http_client_read(client, buf, EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
        if (cur_data_len < 0) {
            ESP_LOGE(TAG, "read http data failed:%d", cur_data_len);
            goto exit;
        } else if (cur_data_len > 0) {
            if (image_header_checked == false) {
                memcpy(&update_app_desc, &buf[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                ESP_LOGI(TAG, "update firmware_version:%s", update_app_desc.version);

                if (memcmp(update_app_desc.version, running_app_desc.version, sizeof(update_app_desc.version)) == 0) { // version check
                    ESP_LOGE(TAG, "running and update firmware_version are same");
                    goto exit;
                }

                err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle); // earse partition
                if (ESP_OK != err) {
                    ESP_LOGE(TAG, "esp_ota_begin failed:%s", esp_err_to_name(err));
                    goto exit;
                }

                image_header_checked = true;
                ESP_LOGI(TAG, "esp_ota_begin ok");
            }

            err = esp_ota_write(update_handle, buf, cur_data_len); // write partition
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed:%s", esp_err_to_name(err));
                goto exit;
            }

            total_data_len += cur_data_len;
            ESP_LOGI(TAG, "received and written image length, cur:%d totol:%d", cur_data_len, total_data_len);
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

    err = esp_ota_end(update_handle); // verify image
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "esp_ota_end failed:%s", esp_err_to_name(err));
        goto exit;
    }
    ESP_LOGI(TAG, "image verify ok");

    err = esp_ota_set_boot_partition(update_partition); // write otadata partition for bootloader to boot
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed:%s", esp_err_to_name(err));
        goto exit;
    }

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
