#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include <string.h>


#define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
#define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_HTTP_SERVER_HOST                    "www.howsmyssl.com"
#define EXAMPLE_HTTP_SERVER_PORT                    443

extern const char howsmyssl_root_cert_pem_start[]   asm("_binary_howsmyssl_root_cert_pem_start");
extern const char howsmyssl_root_cert_pem_end[]     asm("_binary_howsmyssl_root_cert_pem_end");

static const char get_request[] = "GET https://www.howsmyssl.com/a/check HTTP/1.1\r\n"
                                  "Host: "EXAMPLE_HTTP_SERVER_HOST"\r\n"
                                  "User-Agent: esp-idf/5.2.2 esp32\r\n"
                                  "\r\n";

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_net_context net_ctx;
static mbedtls_ssl_context ssl_ctx;
static mbedtls_ssl_config ssl_conf;
static mbedtls_x509_crt cacert;

static const char *TAG = "http_mbedtls";

static void start_http_client_task(void *pvParameters)
{
    uint8_t buf[512] = {0};
    char port[16] = {0};
    int ret = 0;
    const int ciphersuites[] = {MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
                                MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
                                MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
                                MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
                                MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
                                MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,
                                MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
                                MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384};

    mbedtls_net_init(&net_ctx);
    mbedtls_ssl_init(&ssl_ctx);
    mbedtls_ssl_config_init(&ssl_conf);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    mbedtls_x509_crt_parse(&cacert, (unsigned char *)howsmyssl_root_cert_pem_start, howsmyssl_root_cert_pem_end - howsmyssl_root_cert_pem_start);
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    
    ESP_LOGI(TAG, "Connecting to %s:%d", EXAMPLE_HTTP_SERVER_HOST, EXAMPLE_HTTP_SERVER_PORT);
    sprintf(port, "%d", EXAMPLE_HTTP_SERVER_PORT);
    if ((ret = mbedtls_net_connect(&net_ctx, EXAMPLE_HTTP_SERVER_HOST, port, MBEDTLS_NET_PROTO_TCP)) != 0) {
        ESP_LOGE(TAG, "mbedtls_net_connect returned -%x", -ret);
        goto exit;
    }
    ESP_LOGI(TAG, "mbedtls_net_connect ok");

    mbedtls_ssl_config_defaults(&ssl_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_ciphersuites(&ssl_conf, ciphersuites);
    mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&ssl_conf, &cacert, NULL); // esp_crt_bundle_attach(&ssl_conf); 
    mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ssl_ctx, &ssl_conf)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x", -ret);
        goto exit;
    }
    ESP_LOGI(TAG, "mbedtls_ssl_setup ok");
    
    mbedtls_ssl_set_hostname(&ssl_ctx, EXAMPLE_HTTP_SERVER_HOST);
    mbedtls_ssl_set_bio(&ssl_ctx, &net_ctx, mbedtls_net_send, mbedtls_net_recv, NULL);

    ESP_LOGI(TAG, "Performing handshake");
    while ((ret = mbedtls_ssl_handshake(&ssl_ctx)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
            goto exit;
        }
    }

    ESP_LOGI(TAG, "Verifying peer X.509 certificate");
    if ((ret = mbedtls_ssl_get_verify_result(&ssl_ctx)) != 0) {
        ESP_LOGW(TAG, "verify peer certificate failed");
    }
    else {
        ESP_LOGI(TAG, "verify peer certificate ok");
    }

    ESP_LOGI(TAG, "Cipher suite: %s", mbedtls_ssl_get_ciphersuite(&ssl_ctx));

    ESP_LOGI(TAG, "Writing HTTP request");
    mbedtls_ssl_write(&ssl_ctx, (unsigned char *)get_request, strlen(get_request));

    ESP_LOGI(TAG, "Reading HTTP response");
    while(1) {
        memset(buf, 0, sizeof(buf));
        ret = mbedtls_ssl_read(&ssl_ctx, buf, sizeof(buf));
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            ESP_LOGW(TAG, "connection closed");
            break;
        }
        else if (ret < 0) {
            ESP_LOGE(TAG, "mbedtls_ssl_read returned -0x%x", -ret);
            break;
        }

        ESP_LOGI(TAG, "%d bytes read", ret);
        ESP_LOGI(TAG, "%s", buf);
        // ESP_LOG_BUFFER_HEX(TAG, buf, ret);
    }

exit:
    mbedtls_net_free(&net_ctx);
    mbedtls_ssl_close_notify(&ssl_ctx);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl_ctx);
    mbedtls_ssl_config_free(&ssl_conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
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
            xTaskCreate(start_http_client_task, "http_client_task", 4096, NULL, 5, NULL); // StackType_t:uint32_t
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
