#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"


#define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_HTTP_SERVER_HOST                    "httpbin.org"
#define EXAMPLE_HTTP_RESPONSE_BUF_SIZE              1024
#define EXAMPLE_USE_CRT_BUNDLE                      0

#if EXAMPLE_USE_CRT_BUNDLE == 0
extern const char howsmyssl_root_cert_pem_start[]   asm("_binary_howsmyssl_root_cert_pem_start");
extern const char howsmyssl_root_cert_pem_end[]     asm("_binary_howsmyssl_root_cert_pem_end");
#endif

static const char *TAG = "http_client";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER: // recv http response header
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, %s:%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:   // recv http response body, remove length and \r\n for Transfer-Encoding:chunked
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, evt_len:%d", evt->data_len);
        uint32_t i = 0;
        for (i = 0; i < evt->data_len / EXAMPLE_HTTP_RESPONSE_BUF_SIZE; i++) {
            ESP_LOG_BUFFER_HEX(TAG, evt->data + i * EXAMPLE_HTTP_RESPONSE_BUF_SIZE, EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
        }
        if (evt->data_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE) {
            ESP_LOG_BUFFER_HEX(TAG, evt->data + i * EXAMPLE_HTTP_RESPONSE_BUF_SIZE, evt->data_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static void http_perform_evt_nonchunk(void)
{
    esp_http_client_config_t config = {
        .host = EXAMPLE_HTTP_SERVER_HOST,
        .path = "/get",
        .query = "k1=20&k2=abcd", // URI:/get?k1=20&k2=abcd
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "start http get request");
    esp_err_t err = esp_http_client_perform(client);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "HTTP GET failed, status:%d err:%s", esp_http_client_get_status_code(client), esp_err_to_name(err));
    }

    // const char *post_data = "hello world 123";
    const char *post_data = "{\"field1\":\"value1\"}";
    esp_http_client_set_url(client, "/post");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    // esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    ESP_LOGI(TAG, "start http post request");
    err = esp_http_client_perform(client);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "HTTP GET failed, status:%d err:%s", esp_http_client_get_status_code(client), esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void http_perform_evt_chunk(void)
{
    esp_http_client_config_t config = {
        .url = "http://"EXAMPLE_HTTP_SERVER_HOST"/stream-bytes/2500",
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "start http get request chunk");
    // esp_http_client_set_header(client, "Range", "bytes=11-20"); // 断点下载
    esp_err_t err = esp_http_client_perform(client);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "HTTP GET chunk failed, status:%d err:%s", esp_http_client_get_status_code(client), esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void http_nonperform_nonevt(void)
{
    char response[EXAMPLE_HTTP_RESPONSE_BUF_SIZE] = {0};
    int response_len = 0;
    int content_len = 0;
    uint32_t i = 0;

    esp_http_client_config_t config = {
        .url = "http://"EXAMPLE_HTTP_SERVER_HOST"/get",
        // .url = "http://"EXAMPLE_HTTP_SERVER_HOST"/stream-bytes/2500",
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "start http get request");
    // 1. setup tcp connect
    // 2. send GET request header
    //     write_len >= 0 - "Content-Length":write_len
    //     write_len < 0  - "Transfer-Encoding":"chunked"
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        content_len = esp_http_client_fetch_headers(client); // 3. recv GET response header
        if (content_len < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } else {
            if (!esp_http_client_is_chunked_response(client)) {
                ESP_LOGI(TAG, "Content-Length: %lld", esp_http_client_get_content_length(client));
                for (i = 0; i < content_len / EXAMPLE_HTTP_RESPONSE_BUF_SIZE; i++) {
                    esp_http_client_read(client, response, EXAMPLE_HTTP_RESPONSE_BUF_SIZE); // 4. recv GET response body
                    ESP_LOG_BUFFER_HEX(TAG, response, EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
                }
                if (content_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE) {
                    esp_http_client_read(client, response, content_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
                    ESP_LOG_BUFFER_HEX(TAG, response, content_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
                }
            } else {
                ESP_LOGI(TAG, "Transfer-Encoding: chunked");
                while (1) {
                    response_len = esp_http_client_read(client, response, EXAMPLE_HTTP_RESPONSE_BUF_SIZE); // remove length and \r\n for Transfer-Encoding:chunked
                    if (response_len > 0) {
                        ESP_LOG_BUFFER_HEX(TAG, response, response_len);
                    } else if (response_len == 0) { // read complete
                        break;
                    } else {
                        ESP_LOGE(TAG, "Failed to read response");
                        break;
                    }
                }
            }
        }
    }
    esp_http_client_close(client); // 5. close tcp connect

    ESP_LOGI(TAG, "start http post request");
    const char *post_data = "{\"field1\":\"value1\"}";
    esp_http_client_set_url(client, "http://"EXAMPLE_HTTP_SERVER_HOST"/post");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    // 1. setup tcp connect
    // 2. send POST request header
    //     write_len >= 0 - "Content-Length":write_len
    //     write_len < 0  - "Transfer-Encoding":"chunked"
    err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data)); // 3. send POST request body
        if (wlen < 0) {
            ESP_LOGE(TAG, "HTTP client send request body failed");
        }
        content_len = esp_http_client_fetch_headers(client); // 4. recv POST response header
        if (content_len < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } else {
            ESP_LOGI(TAG, "Content-Length: %lld", esp_http_client_get_content_length(client));
            for (i = 0; i < content_len / EXAMPLE_HTTP_RESPONSE_BUF_SIZE; i++) {
                esp_http_client_read(client, response, EXAMPLE_HTTP_RESPONSE_BUF_SIZE); // 5. recv POST response body
                ESP_LOG_BUFFER_HEX(TAG, response, EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
            }
            if (content_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE) {
                esp_http_client_read(client, response, content_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
                ESP_LOG_BUFFER_HEX(TAG, response, content_len % EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
            }
        }
    }

    // 6. close tcp connect
    // 7. free HTTP client resource
    esp_http_client_cleanup(client); 
}

static void https_perform_evt_nonchunk(void)
{
    esp_http_client_config_t config = {
        .host = "www.howsmyssl.com",
        .path = "/",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
#if EXAMPLE_USE_CRT_BUNDLE == 0
        .cert_pem = howsmyssl_root_cert_pem_start,
#else
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "start https get request");
    esp_err_t err = esp_http_client_perform(client);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "HTTPS GET failed, status:%d err:%s", esp_http_client_get_status_code(client), esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void http_client_task(void *pvParameters)
{
    http_perform_evt_nonchunk();
    http_perform_evt_chunk();
    http_nonperform_nonevt();
    https_perform_evt_nonchunk();
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
            xTaskCreate(http_client_task, "http_client_task", 4096, NULL, 5, NULL); // StackType_t:uint32_t
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
