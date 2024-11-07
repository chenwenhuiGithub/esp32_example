#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "mbedtls/md.h"
#include "mbedtls/ssl.h"
#include "cJSON.h"
#include "cloud.h"
#include "ota.h"


typedef struct {
    char url[256];
    char version[16];
    char sha256[128];
    uint32_t size;
    uint32_t download_size;
} ota_task_t;

extern const uint8_t remote_server_root_crt_start[]     asm("_binary_remote_server_root_crt_start");
extern const uint8_t remote_server_root_crt_end[]       asm("_binary_remote_server_root_crt_end");
extern const uint8_t local_ota_sign_pub_key_start[]     asm("_binary_local_ota_sign_pub_key_start");
extern const uint8_t local_ota_sign_pub_key_end[]       asm("_binary_local_ota_sign_pub_key_end");

static ota_task_t s_ota_task = {0};
static esp_ota_handle_t s_hd_ota = 0;
static esp_http_client_handle_t s_hd_http = NULL;
static const char *TAG = "ota";


static void ota_report_progress_task(void *pvParameters) {
    uint8_t progress = 0;
    static uint8_t last_progress = 0;
    char buf[256] = {0};
    
    while (1) {
        progress = s_ota_task.download_size * 100 / s_ota_task.size;
        if (last_progress != progress) {
            sprintf(buf, "{\"id\":\"%s\", \"params\":{\"step\":\"%u\", \"desc\":\"success\"}}", cloud_gen_msg_id(), progress);
            cloud_send_publish(CONFIG_TOPIC_OTA_REPORT_PROGRESS, (uint8_t *)buf, strlen(buf));
            ESP_LOGI(TAG, "report progress: %u%%", progress);
            last_progress = progress;        
        }
        vTaskDelay(CONFIG_OTA_REPORT_PROGRESS_PERIOD / portTICK_PERIOD_MS);
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
        .cert_pem = (char *)remote_server_root_crt_start,
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

void ota_start(uint8_t *payload, uint32_t len) {
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

void ota_report_version() {
    const esp_partition_t *part = NULL;
    esp_app_desc_t app_desc = {0};
    char buf[256] = {0};

    part = esp_ota_get_running_partition();
    esp_partition_read(part, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), &app_desc, sizeof(esp_app_desc_t));
    sprintf(buf, "{\"id\":\"%s\", \"params\":{\"version\":\"%s\"}}", cloud_gen_msg_id(), app_desc.version);
    cloud_send_publish(CONFIG_TOPIC_OTA_REPORT_VERSION, (uint8_t *)buf, strlen(buf));
    ESP_LOGI(TAG, "ota report version:%s", app_desc.version);
}

static int verify_signature(const esp_partition_t *part, uint32_t file_size) {
    int ret = 0;
    mbedtls_pk_context pk;
    mbedtls_md_context_t ctx;
    uint8_t hash_data[32] = {0};
    uint8_t sign_data[256] = {0};
    uint8_t buf[1024] = {0};
    uint32_t image_size = file_size - 256; // file_size = image + signature
    uint32_t i = 0;
    uint32_t quotient = image_size / 1024;
    uint32_t remainder = image_size % 1024;

    mbedtls_pk_init(&pk);
    ret = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)local_ota_sign_pub_key_start, local_ota_sign_pub_key_end - local_ota_sign_pub_key_start);
    if (ret) {
        ESP_LOGE(TAG, "mbedtls_pk_parse_public_key failed:%d", ret);
        mbedtls_pk_free(&pk);
        return ret;
    }

    mbedtls_md_init(&ctx);  
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);  
    mbedtls_md_starts(&ctx); 
    while (i < quotient) {
        esp_partition_read(part, i * 1024, buf, 1024);
        mbedtls_md_update(&ctx, buf, 1024);
        i++;
    }
    if (remainder != 0) {
        esp_partition_read(part, i * 1024, buf, remainder);
        mbedtls_md_update(&ctx, buf, remainder);
    }
    mbedtls_md_finish(&ctx, hash_data);  
    mbedtls_md_free(&ctx);  

    esp_partition_read(part, image_size, sign_data, 256);

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash_data, 32, sign_data, 256);
    if (ret) {
        ESP_LOGE(TAG, "mbedtls_pk_verify failed:%d", ret);
    }
    mbedtls_pk_free(&pk);

    return ret;
}

esp_err_t http_ota_update_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    char buf[1024] = {0};
    size_t header_len = 0, file_size = 0, file_read = 0;
    int content_length = 0, read_len = 0, boundary_len = 0;
    char *boundary = NULL, *file_begin = NULL;
    const char end_line[4] = {'\r', '\n', '\r', '\n'};
    const esp_partition_t *part = NULL;

    header_len = httpd_req_get_hdr_value_len(req, "Content-Length");
    httpd_req_get_hdr_value_str(req, "Content-Length", buf, header_len + 1);
    content_length = atoi(buf);
    ESP_LOGI(TAG, "Content-Length:%d", content_length);

    header_len = httpd_req_get_hdr_value_len(req, "Content-Type");
    httpd_req_get_hdr_value_str(req, "Content-Type", buf, header_len + 1);
    boundary = strstr(buf, "----");
    boundary_len = strlen(boundary);
    ESP_LOGI(TAG, "boundary:%s", boundary);

    // first package, include First boundary,Content-Disposition,Content-Type,filedata
    read_len = httpd_req_recv(req, buf, sizeof(buf));
    file_begin = memmem(buf, read_len, end_line, sizeof(end_line));
    file_begin += sizeof(end_line);
    file_size = content_length - (file_begin - buf) - boundary_len - 8;
    file_read = read_len - (file_begin - buf);

    part = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "ota partition, address:0x%08"PRIx32" size:0x%08"PRIx32"", part->address, part->size);
    err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &s_hd_ota);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "esp_ota_begin failed:%s", esp_err_to_name(err));
        goto exit;
    }
    esp_ota_write(s_hd_ota, file_begin, file_read);

    while (file_read < file_size) {
        read_len = httpd_req_recv(req, buf, sizeof(buf));
        if (read_len <= 0) {
            if (read_len == MBEDTLS_ERR_SSL_WANT_READ || read_len == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            ESP_LOGE(TAG, "httpd_req_recv failed:%d", read_len);
            goto exit;
        }

        if (read_len > file_size - file_read) { // last package, maybe include Last boundary
            read_len = file_size - file_read;
        }
        esp_ota_write(s_hd_ota, buf, read_len);
        file_read += read_len;
        ESP_LOGI(TAG, "httpd_req_recv:%d", read_len);
    }
    ESP_LOGI(TAG, "file_size:%u file_read:%u", file_size, file_read);

    if (verify_signature(part, file_size)) {
        ESP_LOGE(TAG, "signature check failed");
        goto exit;
    }
    ESP_LOGI(TAG, "signature check ok");

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

    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, "{\"code\":0, \"message\":\"success\"}", strlen("{\"code\":0, \"message\":\"success\"}"));

    ESP_LOGI(TAG, "restart after 3s");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();

exit:
    esp_ota_abort(s_hd_ota);
    httpd_resp_set_status(req, HTTPD_500);
    httpd_resp_send(req, "{\"code\":1, \"message\":\"failed\"}", strlen("{\"code\":1, \"message\":\"failed\"}"));

    return ESP_FAIL;
}
