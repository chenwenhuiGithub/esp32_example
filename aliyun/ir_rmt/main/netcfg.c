#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "ota.h"
#include "netcfg.h"


#define CONFIG_NETCFG_MAX_LEN_SSID                          32
#define CONFIG_NETCFG_MAX_LEN_PWD                           64
#define CONFIG_NETCFG_NVS_NAMESPACE                         "netcfg"
#define CONFIG_NETCFG_NVS_KEY_SSID                          "sta_ssid"
#define CONFIG_NETCFG_NVS_KEY_PWD                           "sta_pwd"


extern const uint8_t index_html_gz_start[]                  asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]                    asm("_binary_index_html_gz_end");

extern const uint8_t local_https_server_crt_start[]         asm("_binary_local_https_server_crt_start");
extern const uint8_t local_https_server_crt_end[]           asm("_binary_local_https_server_crt_end");
extern const uint8_t local_https_server_priv_key_start[]    asm("_binary_local_https_server_priv_key_start");
extern const uint8_t local_https_server_priv_key_end[]      asm("_binary_local_https_server_priv_key_end");

netcfg_netstat_t s_netstat = NETSTAT_WIFI_NOT_CONNECTED;
static httpd_handle_t s_hd_httpd = NULL;
static nvs_handle_t s_hd_nvs = 0;
static const char *TAG = "netcfg";

static void led_init() {
    gpio_config_t led_conf = {0};

    led_conf.intr_type = GPIO_INTR_DISABLE;
    led_conf.pin_bit_mask = 1ULL << CONFIG_GPIO_NUM_NETCFG_LED;
    led_conf.mode = GPIO_MODE_OUTPUT;
    led_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    led_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&led_conf);

    gpio_set_level(CONFIG_GPIO_NUM_NETCFG_LED, 0); // 0 - off, 1 - on
}

static void show_netstat_task(void* parameter) {	
    while (1) {
        if (NETSTAT_WIFI_NOT_CONNECTED == s_netstat) {
            gpio_set_level(CONFIG_GPIO_NUM_NETCFG_LED, 1);
            vTaskDelay(300 / portTICK_PERIOD_MS);
            gpio_set_level(CONFIG_GPIO_NUM_NETCFG_LED, 0);
            vTaskDelay(300 / portTICK_PERIOD_MS);
        } else if (NETSTAT_WIFI_CONNECTED == s_netstat) {
            gpio_set_level(CONFIG_GPIO_NUM_NETCFG_LED, 1);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            gpio_set_level(CONFIG_GPIO_NUM_NETCFG_LED, 0);
            vTaskDelay(1000 / portTICK_PERIOD_MS);            
        } else if (NETSTAT_CLOUD_CONNECTED == s_netstat) {
            gpio_set_level(CONFIG_GPIO_NUM_NETCFG_LED, 1);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

static esp_err_t http_get_index_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

static esp_err_t http_cfg_wifi_handler(httpd_req_t *req) {
    char post_data[256] = {0};
    cJSON *root = NULL, *ssid_json = NULL, *pwd_json = NULL;
    wifi_config_t sta_cfg = {0};

    httpd_req_recv(req, post_data, sizeof(post_data));
    ESP_LOGI(TAG, "http post data:%s", post_data);

    root = cJSON_Parse(post_data);
    ssid_json = cJSON_GetObjectItem(root, "ssid");
    pwd_json = cJSON_GetObjectItem(root, "pwd");
    if (strlen(ssid_json->valuestring) > CONFIG_NETCFG_MAX_LEN_SSID || strlen(pwd_json->valuestring) > CONFIG_NETCFG_MAX_LEN_PWD) {
        ESP_LOGE(TAG, "ssid or pwd too long");
        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_send(req, "{\"message\":\"failed\"}", strlen("{\"message\":\"failed\"}"));
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, "{\"message\":\"success\"}", strlen("{\"message\":\"success\"}"));

    memcpy(sta_cfg.sta.ssid, ssid_json->valuestring, strlen(ssid_json->valuestring));
    memcpy(sta_cfg.sta.password, pwd_json->valuestring, strlen(pwd_json->valuestring));
    cJSON_Delete(root);

    netcfg_set_wifi_info((char *)sta_cfg.sta.ssid, (char *)sta_cfg.sta.password);
    ESP_LOGI(TAG, "set wifi netcfg info, %s:%s", sta_cfg.sta.ssid, sta_cfg.sta.password);

    netcfg_set_netstat(NETSTAT_WIFI_NOT_CONNECTED);
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_connect();
    ESP_LOGI(TAG, "wifi start connect");

    return ESP_OK;
}

void netcfg_init() {
    esp_err_t ret = ESP_OK;
    const httpd_uri_t uri_get_index = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = http_get_index_handler,
        .user_ctx  = NULL,
    };
    const httpd_uri_t uri_cfg_wifi = {
        .uri       = "/cfg_wifi",
        .method    = HTTP_POST,
        .handler   = http_cfg_wifi_handler,
        .user_ctx  = NULL,
    };
    const httpd_uri_t uri_ota_update = {
        .uri       = "/ota_update",
        .method    = HTTP_POST,
        .handler   = http_ota_update_handler,
        .user_ctx  = NULL,
    };

    httpd_ssl_config_t httpd_cfg = HTTPD_SSL_CONFIG_DEFAULT();
    httpd_cfg.servercert = local_https_server_crt_start;
    httpd_cfg.servercert_len = local_https_server_crt_end - local_https_server_crt_start;
    httpd_cfg.prvtkey_pem = local_https_server_priv_key_start;
    httpd_cfg.prvtkey_len = local_https_server_priv_key_end - local_https_server_priv_key_start;
    // httpd_cfg.cacert_pem = client_root_crt_start;
    // httpd_cfg.cacert_len = client_root_crt_end - client_root_crt_start;
    httpd_cfg.httpd.stack_size = 6144; // default:10240
    ret = httpd_ssl_start(&s_hd_httpd, &httpd_cfg);
    if (ESP_OK == ret) {
        ESP_LOGI(TAG, "httpd start ok, port:%d", httpd_cfg.port_secure);
        httpd_register_uri_handler(s_hd_httpd, &uri_get_index);
        httpd_register_uri_handler(s_hd_httpd, &uri_cfg_wifi);
        httpd_register_uri_handler(s_hd_httpd, &uri_ota_update);

        led_init();
        xTaskCreate(show_netstat_task, "show_netstat_task", 1024, NULL, 1, NULL);
    } else {
        ESP_LOGE(TAG, "httpd start failed:%d", ret);
    }
}

void netcfg_get_netstat(netcfg_netstat_t *stat) {
    *stat = s_netstat;
}

void netcfg_set_netstat(netcfg_netstat_t stat) {
    s_netstat = stat;
}

void netcfg_get_wifi_info(char *ssid, char *pwd) {
    size_t ssid_len = CONFIG_NETCFG_MAX_LEN_SSID;
    size_t pwd_len = CONFIG_NETCFG_MAX_LEN_PWD;

    nvs_open(CONFIG_NETCFG_NVS_NAMESPACE, NVS_READONLY, &s_hd_nvs);
    nvs_get_str(s_hd_nvs, CONFIG_NETCFG_NVS_KEY_SSID, ssid, &ssid_len);
    nvs_get_str(s_hd_nvs, CONFIG_NETCFG_NVS_KEY_PWD, pwd, &pwd_len);
    nvs_close(s_hd_nvs);
}

void netcfg_set_wifi_info(char *ssid, char *pwd) {
    nvs_open(CONFIG_NETCFG_NVS_NAMESPACE, NVS_READWRITE, &s_hd_nvs);
    nvs_set_str(s_hd_nvs, CONFIG_NETCFG_NVS_KEY_SSID, ssid);
    nvs_set_str(s_hd_nvs, CONFIG_NETCFG_NVS_KEY_PWD, pwd);
    nvs_commit(s_hd_nvs);
    nvs_close(s_hd_nvs);
}
