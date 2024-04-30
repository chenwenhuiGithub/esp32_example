#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "kv";

void app_main(void)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t handle_ns;
    
    const char* k_restart = "restart_cnt";
    uint32_t v_restart = 0;
    const char* k_password = "password";
    uint8_t v_password[8] = {0};
    
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_flash_init error:%d", err);
        return;
    }

    err = nvs_open("ns", NVS_READWRITE, &handle_ns);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_open error:%d", err);
        return;
    }

    err = nvs_get_u32(handle_ns, k_restart, &v_restart);
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "%s get success:%u", k_restart, v_restart);
        v_restart++;
    } else if (ESP_ERR_NVS_NOT_FOUND == err) {
        ESP_LOGI(TAG, "%s not set", k_restart);
        v_restart = 0;
    } else {
        ESP_LOGE(TAG, "%s get error:%d", k_restart, err);
    }
    err = nvs_set_u32(handle_ns, k_restart, v_restart);
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "%s set success:%u", k_restart, v_restart);
    } else {
        ESP_LOGE(TAG, "%s set error:%d", k_restart, err);
    }

    size_t required_size = 0;
    err = nvs_get_blob(handle_ns, k_password, NULL, &required_size);
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "%s size get success:%u", k_password, (uint32_t)required_size);
        nvs_get_blob(handle_ns, k_password, v_password, &required_size);
        ESP_LOGI(TAG, "%s data get success:%02x %02x %02x %02x %02x %02x %02x %02x",
                 k_password, v_password[0], v_password[1], v_password[2], v_password[3], v_password[4], v_password[5], v_password[6], v_password[7]);
        v_password[0]++;
        v_password[1]++;
        v_password[2]++;
        v_password[3]++;
        v_password[4]++;
        v_password[5]++;
        v_password[6]++;
        v_password[7]++;
    } else if (ESP_ERR_NVS_NOT_FOUND == err) {
        ESP_LOGI(TAG, "%s not set", k_password);
        v_password[0] = 0x00;
        v_password[1] = 0x11;
        v_password[2] = 0x22;
        v_password[3] = 0x33;
        v_password[4] = 0xaa;
        v_password[5] = 0xbb;
        v_password[6] = 0xcc;
        v_password[7] = 0xdd;
    } else {
        ESP_LOGE(TAG, "%s get error:%d", k_password, err);
    }
    nvs_set_blob(handle_ns, k_password, v_password, sizeof(v_password));
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "%s data set success:%02x %02x %02x %02x %02x %02x %02x %02x",
                 k_password, v_password[0], v_password[1], v_password[2], v_password[3], v_password[4], v_password[5], v_password[6], v_password[7]);
    } else {
        ESP_LOGE(TAG, "%s set error:%d", k_password, err);
    }

    nvs_commit(handle_ns);
    nvs_close(handle_ns);
}
