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
#include "mbedtls/ssl_ciphersuites.h"
#include <string.h>


// #define EXAMPLE_WIFI_SSID                           "TP-LINK_wenhui"
// #define EXAMPLE_WIFI_PWD                            "12345678"
#define EXAMPLE_WIFI_SSID                           "SolaxGuest"
#define EXAMPLE_WIFI_PWD                            "solaxpower"
#define EXAMPLE_HTTP_SERVER_PORT                    "4443"

extern const uint8_t mosquitto_crt_start[]          asm("_binary_mosquitto_crt_start");
extern const uint8_t mosquitto_crt_end[]            asm("_binary_mosquitto_crt_end");
extern const uint8_t server_crt_start[]             asm("_binary_server_crt_start");
extern const uint8_t server_crt_end[]               asm("_binary_server_crt_end");
extern const uint8_t server_priv_key_start[]        asm("_binary_server_priv_key_start");
extern const uint8_t server_priv_key_end[]          asm("_binary_server_priv_key_end");

static const char get_response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" \
                                    "<h2>Mbed TLS Test Server</h2>\r\n" \
                                    "<p>Successful connection</p>\r\n";

static const char *TAG = "mbedtls_http_server";


static char *get_key_exchange_string(mbedtls_key_exchange_type_t type) {
    switch (type) {
        case MBEDTLS_KEY_EXCHANGE_NONE: return "NONE";
        case MBEDTLS_KEY_EXCHANGE_RSA: return "RSA";
        case MBEDTLS_KEY_EXCHANGE_DHE_RSA: return "DHE_RSA";
        case MBEDTLS_KEY_EXCHANGE_ECDHE_RSA: return "ECDHE_RSA";
        case MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA: return "ECDHE_ECDSA";
        case MBEDTLS_KEY_EXCHANGE_PSK: return "PSK";
        case MBEDTLS_KEY_EXCHANGE_DHE_PSK: return "DHE_PSK";
        case MBEDTLS_KEY_EXCHANGE_RSA_PSK: return "RSA_PSK";
        case MBEDTLS_KEY_EXCHANGE_ECDHE_PSK: return "ECDHE_PSK";
        case MBEDTLS_KEY_EXCHANGE_ECDH_RSA: return "ECDH_RSA";
        case MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA: return "ECDH_ECDSA";
        case MBEDTLS_KEY_EXCHANGE_ECJPAKE: return "ECJPAKE";
        default: return "unknown";
    }
}

static char *get_protocol_version_string(mbedtls_ssl_protocol_version version) {
    switch (version) {
        case MBEDTLS_SSL_VERSION_TLS1_2: return "TLS1_2";
        case MBEDTLS_SSL_VERSION_TLS1_3: return "TLS1_3";
        default: return "unknown";
    }
}

static void http_client_task(void *pvParameters)
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_net_context listen_fd, client_fd;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config ssl_conf;
    mbedtls_x509_crt server_crt;
    mbedtls_pk_context pk_ctx;
    uint8_t buf[512] = {0};
    int ret = 0;
    const int *list = NULL;
    const mbedtls_ssl_ciphersuite_t * ciphersuite_info = NULL;

    ESP_LOGI(TAG, "supported ciphersuite list:");
    list = mbedtls_ssl_list_ciphersuites();
    while (*list) {
        ciphersuite_info = mbedtls_ssl_ciphersuite_from_id(*list);
        ESP_LOGI(TAG, "0x%04X %s/%s %-12s %s", (*list),
                                            get_protocol_version_string(ciphersuite_info->private_min_tls_version),
                                            get_protocol_version_string(ciphersuite_info->private_max_tls_version),
                                            get_key_exchange_string(ciphersuite_info->private_key_exchange),
                                            mbedtls_ssl_get_ciphersuite_name(*list));
        list++;
    }

    mbedtls_net_init(&listen_fd);
    mbedtls_net_init(&client_fd);
    mbedtls_ssl_init(&ssl_ctx);
    mbedtls_ssl_config_init(&ssl_conf);
    mbedtls_x509_crt_init(&server_crt);
    mbedtls_pk_init(&pk_ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);

    mbedtls_x509_crt_parse(&server_crt, server_crt_start, server_crt_end - server_crt_start);           // server crt
    mbedtls_x509_crt_parse(&server_crt, mosquitto_crt_start, mosquitto_crt_end - mosquitto_crt_start);  // issuer crt
    mbedtls_pk_parse_key(&pk_ctx, server_priv_key_start, server_priv_key_end - server_priv_key_start, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg);

    mbedtls_net_bind(&listen_fd, NULL, EXAMPLE_HTTP_SERVER_PORT, MBEDTLS_NET_PROTO_TCP);

    mbedtls_ssl_config_defaults(&ssl_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_ca_chain(&ssl_conf, server_crt.next, NULL);
    mbedtls_ssl_conf_own_cert(&ssl_conf, &server_crt, &pk_ctx);
    mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    if ((ret = mbedtls_ssl_setup(&ssl_ctx, &ssl_conf)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_setup failed:-0x%x", -ret);
        goto exit;
    }
    ESP_LOGI(TAG, "mbedtls_ssl_setup ok");
    
    mbedtls_net_accept(&listen_fd, &client_fd, NULL, 0, NULL);
    ESP_LOGI(TAG, "mbedtls_net_accept ok");

    mbedtls_ssl_set_bio(&ssl_ctx, &client_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ssl_ctx)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "mbedtls_ssl_handshake failed:-0x%x", -ret);
            goto exit;
        }
    }
    ESP_LOGI(TAG, "mbedtls_ssl_handshake ok");

    ESP_LOGI(TAG, "ciphersuite:%s", mbedtls_ssl_get_ciphersuite(&ssl_ctx));

    memset(buf, 0, sizeof(buf));
    ret = mbedtls_ssl_read(&ssl_ctx, buf, sizeof(buf));
    ESP_LOGI(TAG, "recv HTTP request, len:%d", ret);
    ESP_LOGI(TAG, "%s", buf);
    // ESP_LOG_BUFFER_HEX(TAG, buf, ret);

    mbedtls_ssl_write(&ssl_ctx, (unsigned char *)get_response, strlen(get_response));
    ESP_LOGI(TAG, "send HTTP response");

exit:
    mbedtls_ssl_close_notify(&ssl_ctx);
    mbedtls_net_free(&listen_fd);
    mbedtls_net_free(&client_fd);
    mbedtls_x509_crt_free(&server_crt);
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
            xTaskCreate(http_client_task, "http_client_task", 8192, NULL, 5, NULL);
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
    uint32_t i = 0;
    
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
    esp_wifi_start();

    while (1) {
        ESP_LOGI(TAG, "%lu", i++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
