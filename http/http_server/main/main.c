#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_https_server.h"


#define EXAMPLE_WIFI_SSID                       "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                        "12345678"
#define EXAMPLE_HTTP_RESPONSE_BUF_SIZE          1024
#define EXAMPLE_USE_HTTPS                       0

#if EXAMPLE_USE_HTTPS == 1
static const uint8_t server_cert_pem[]  =
"-----BEGIN CERTIFICATE-----\n"
"MIIDKzCCAhOgAwIBAgIUBxM3WJf2bP12kAfqhmhhjZWv0ukwDQYJKoZIhvcNAQEL"
"BQAwJTEjMCEGA1UEAwwaRVNQMzIgSFRUUFMgc2VydmVyIGV4YW1wbGUwHhcNMTgx"
"MDE3MTEzMjU3WhcNMjgxMDE0MTEzMjU3WjAlMSMwIQYDVQQDDBpFU1AzMiBIVFRQ"
"UyBzZXJ2ZXIgZXhhbXBsZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB"
"ALBint6nP77RCQcmKgwPtTsGK0uClxg+LwKJ3WXuye3oqnnjqJCwMEneXzGdG09T"
"sA0SyNPwrEgebLCH80an3gWU4pHDdqGHfJQa2jBL290e/5L5MB+6PTs2NKcojK/k"
"qcZkn58MWXhDW1NpAnJtjVniK2Ksvr/YIYSbyD+JiEs0MGxEx+kOl9d7hRHJaIzd"
"GF/vO2pl295v1qXekAlkgNMtYIVAjUy9CMpqaQBCQRL+BmPSJRkXBsYk8GPnieS4"
"sUsp53DsNvCCtWDT6fd9D1v+BB6nDk/FCPKhtjYOwOAZlX4wWNSZpRNr5dfrxKsb"
"jAn4PCuR2akdF4G8WLUeDWECAwEAAaNTMFEwHQYDVR0OBBYEFMnmdJKOEepXrHI/"
"ivM6mVqJgAX8MB8GA1UdIwQYMBaAFMnmdJKOEepXrHI/ivM6mVqJgAX8MA8GA1Ud"
"EwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBADiXIGEkSsN0SLSfCF1VNWO3"
"emBurfOcDq4EGEaxRKAU0814VEmU87btIDx80+z5Dbf+GGHCPrY7odIkxGNn0DJY"
"W1WcF+DOcbiWoUN6DTkAML0SMnp8aGj9ffx3x+qoggT+vGdWVVA4pgwqZT7Ybntx"
"bkzcNFW0sqmCv4IN1t4w6L0A87ZwsNwVpre/j6uyBw7s8YoJHDLRFT6g7qgn0tcN"
"ZufhNISvgWCVJQy/SZjNBHSpnIdCUSJAeTY2mkM4sGxY0Widk8LnjydxZUSxC3Nl"
"hb6pnMh3jRq4h0+5CZielA4/a+TdrNPv/qok67ot/XJdY3qHCCd8O2b14OVq9jo="
"\n"
"-----END CERTIFICATE-----";

static const uint8_t server_privkey_pem[]  =
"-----BEGIN PRIVATE KEY-----\n"
"MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCwYp7epz++0QkH"
"JioMD7U7BitLgpcYPi8Cid1l7snt6Kp546iQsDBJ3l8xnRtPU7ANEsjT8KxIHmyw"
"h/NGp94FlOKRw3ahh3yUGtowS9vdHv+S+TAfuj07NjSnKIyv5KnGZJ+fDFl4Q1tT"
"aQJybY1Z4itirL6/2CGEm8g/iYhLNDBsRMfpDpfXe4URyWiM3Rhf7ztqZdveb9al"
"3pAJZIDTLWCFQI1MvQjKamkAQkES/gZj0iUZFwbGJPBj54nkuLFLKedw7DbwgrVg"
"0+n3fQ9b/gQepw5PxQjyobY2DsDgGZV+MFjUmaUTa+XX68SrG4wJ+DwrkdmpHReB"
"vFi1Hg1hAgMBAAECggEAaTCnZkl/7qBjLexIryC/CBBJyaJ70W1kQ7NMYfniWwui"
"f0aRxJgOdD81rjTvkINsPp+xPRQO6oOadjzdjImYEuQTqrJTEUnntbu924eh+2D9"
"Mf2CAanj0mglRnscS9mmljZ0KzoGMX6Z/EhnuS40WiJTlWlH6MlQU/FDnwC6U34y"
"JKy6/jGryfsx+kGU/NRvKSru6JYJWt5v7sOrymHWD62IT59h3blOiP8GMtYKeQlX"
"49om9Mo1VTIFASY3lrxmexbY+6FG8YO+tfIe0tTAiGrkb9Pz6tYbaj9FjEWOv4Vc"
"+3VMBUVdGJjgqvE8fx+/+mHo4Rg69BUPfPSrpEg7sQKBgQDlL85G04VZgrNZgOx6"
"pTlCCl/NkfNb1OYa0BELqWINoWaWQHnm6lX8YjrUjwRpBF5s7mFhguFjUjp/NW6D"
"0EEg5BmO0ePJ3dLKSeOA7gMo7y7kAcD/YGToqAaGljkBI+IAWK5Su5yldrECTQKG"
"YnMKyQ1MWUfCYEwHtPvFvE5aPwKBgQDFBWXekpxHIvt/B41Cl/TftAzE7/f58JjV"
"MFo/JCh9TDcH6N5TMTRS1/iQrv5M6kJSSrHnq8pqDXOwfHLwxetpk9tr937VRzoL"
"CuG1Ar7c1AO6ujNnAEmUVC2DppL/ck5mRPWK/kgLwZSaNcZf8sydRgphsW1ogJin"
"7g0nGbFwXwKBgQCPoZY07Pr1TeP4g8OwWTu5F6dSvdU2CAbtZthH5q98u1n/cAj1"
"noak1Srpa3foGMTUn9CHu+5kwHPIpUPNeAZZBpq91uxa5pnkDMp3UrLIRJ2uZyr8"
"4PxcknEEh8DR5hsM/IbDcrCJQglM19ZtQeW3LKkY4BsIxjDf45ymH407IQKBgE/g"
"Ul6cPfOxQRlNLH4VMVgInSyyxWx1mODFy7DRrgCuh5kTVh+QUVBM8x9lcwAn8V9/"
"nQT55wR8E603pznqY/jX0xvAqZE6YVPcw4kpZcwNwL1RhEl8GliikBlRzUL3SsW3"
"q30AfqEViHPE3XpE66PPo6Hb1ymJCVr77iUuC3wtAoGBAIBrOGunv1qZMfqmwAY2"
"lxlzRgxgSiaev0lTNxDzZkmU/u3dgdTwJ5DDANqPwJc6b8SGYTp9rQ0mbgVHnhIB"
"jcJQBQkTfq6Z0H6OoTVi7dPs3ibQJFrtkoyvYAbyk36quBmNRjVh6rc8468bhXYr"
"v/t+MeGJP/0Zw8v/X2CFll96\n"
"-----END PRIVATE KEY-----";
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
    config.servercert = server_cert_pem;
    config.servercert_len = sizeof(server_cert_pem);
    config.prvtkey_pem = server_privkey_pem;
    config.prvtkey_len = sizeof(server_privkey_pem);
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
