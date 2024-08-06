#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_https_server.h"


#define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_HTTP_RESPONSE_BUF_SIZE              1024
#define EXAMPLE_USE_HTTPS                           1

#if EXAMPLE_USE_HTTPS == 1
extern const uint8_t server_cert_pem_start[]        asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[]          asm("_binary_server_cert_pem_end");
extern const uint8_t server_prvtkey_pem_start[]     asm("_binary_server_prvtkey_pem_start");
extern const uint8_t server_prvtkey_pem_end[]       asm("_binary_server_prvtkey_pem_end");
#endif

static esp_err_t hello_get_handler(httpd_req_t *req);
static esp_err_t echo_post_handler(httpd_req_t *req);

static const char *TAG = "http_server";

static const httpd_uri_t uri_hello = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    .user_ctx  = "hello world 123",
};

static const httpd_uri_t uri_echo = {
    .uri       = "/echo",
    .method    = HTTP_POST,
    .handler   = echo_post_handler,
    .user_ctx  = NULL,
};

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    char value1[32] = {0};
    char value2[32] = {0};
    size_t buf_len = 0;

    buf_len = httpd_req_get_hdr_value_len(req, "Host"); // recv GET request hader
    if (buf_len > 0) {
        httpd_req_get_hdr_value_str(req, "Host", buf, buf_len + 1);
        ESP_LOGI(TAG, "Found header => Host:%s", buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Request-Header");
    if (buf_len > 0) {
        httpd_req_get_hdr_value_str(req, "Request-Header", buf, buf_len + 1);
        ESP_LOGI(TAG, "Found header => Request-Header:%s", buf);
    }

    buf_len = httpd_req_get_url_query_len(req); // recv GET request query
    if (buf_len > 0) {
        httpd_req_get_url_query_str(req, buf, buf_len + 1);
        ESP_LOGI(TAG, "Found URL query => %s", buf);
        httpd_query_key_value(buf, "k1", value1, sizeof(value1));
        ESP_LOGI(TAG, "Found URL query parameter => k1:%s", value1);
        httpd_query_key_value(buf, "k2", value2, sizeof(value2));
        ESP_LOGI(TAG, "Found URL query parameter => k2:%s", value2);
    }

    const char* resp_body = (const char*) req->user_ctx;
    // const char *resp_body = "{\"field1\":\"value1\"}";
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_hdr(req, "Response-Header", "abcd");
    httpd_resp_set_type(req, "text/plain");
    // httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_body, strlen(resp_body));

    return ESP_OK;
}

static esp_err_t echo_post_handler(httpd_req_t *req)
{
    char buf[EXAMPLE_HTTP_RESPONSE_BUF_SIZE] = {0};
    size_t buf_len = 0;
    int remaining = req->content_len;
    uint32_t i = 0;

    buf_len = httpd_req_get_hdr_value_len(req, "Host"); // recv POST request header
    if (buf_len > 0) {
        httpd_req_get_hdr_value_str(req, "Host", buf, buf_len + 1);
        ESP_LOGI(TAG, "Found header => Host:%s", buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Request-Header");
    if (buf_len > 0) {
        httpd_req_get_hdr_value_str(req, "Request-Header", buf, buf_len + 1);
        ESP_LOGI(TAG, "Found header => Request-Header:%s", buf);
    }

    for (i = 0; i < remaining / EXAMPLE_HTTP_RESPONSE_BUF_SIZE; i++) {
        httpd_req_recv(req, buf, EXAMPLE_HTTP_RESPONSE_BUF_SIZE); // recv POST request body
        ESP_LOG_BUFFER_HEX(TAG, buf, EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
    }
    if (remaining % EXAMPLE_HTTP_RESPONSE_BUF_SIZE) {
        httpd_req_recv(req, buf, remaining % EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
        ESP_LOG_BUFFER_HEX(TAG, buf, remaining % EXAMPLE_HTTP_RESPONSE_BUF_SIZE);
    }

    httpd_resp_send_chunk(req, "hello world chunk", strlen("hello world chunk"));
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static void start_http_server_task()
{
    httpd_handle_t server = NULL;

#if EXAMPLE_USE_HTTPS == 1
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = server_cert_pem_start;
    config.servercert_len = server_cert_pem_end - server_cert_pem_start;
    config.prvtkey_pem = server_prvtkey_pem_start;
    config.prvtkey_len = server_prvtkey_pem_end - server_prvtkey_pem_start;
    if (httpd_ssl_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "start https server ok, port:%d", config.port_secure);
#else
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "start http server ok, port:%d", config.server_port);
#endif

        httpd_register_uri_handler(server, &uri_hello);
        httpd_register_uri_handler(server, &uri_echo);
    } else {
        ESP_LOGE(TAG, "start http server failed");        
    }
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
            start_http_server_task();
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
