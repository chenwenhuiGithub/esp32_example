#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CONFIG_GPIO_NUM_INMP441_SCK                     4
#define CONFIG_GPIO_NUM_INMP441_WS                      5
#define CONFIG_GPIO_NUM_INMP441_SD                      6
#define CONFIG_GPIO_NUM_MAX98357_BCLK                   15
#define CONFIG_GPIO_NUM_MAX98357_LRC                    16
#define CONFIG_GPIO_NUM_MAX98357_DIN                    7

#define CONFIG_I2S_SAMPLE_BIT                           I2S_DATA_BIT_WIDTH_16BIT
#define CONFIG_I2S_SAMPLE_CHANNEL                       I2S_SLOT_MODE_MONO
#define CONFIG_I2S_SAMPLE_RATE                          16000


static i2s_chan_handle_t hd_tx = NULL;
static i2s_chan_handle_t hd_rx = NULL;
static const char *TAG = "i2s";

static void i2s_rw_task(void *parameter) {
    esp_err_t err = ESP_OK;
    uint8_t buf[2048] = {0};
    size_t len_read = 0;
    size_t len_write = 0;

    while (1) {
        err = i2s_channel_read(hd_rx, buf, sizeof(buf), &len_read, 1000);
        if (ESP_OK == err) {
            // ESP_LOGI(TAG, "i2s read data, len:%lu", len_read);
            // ESP_LOG_BUFFER_HEX(TAG, buf, len_read);
            err = i2s_channel_write(hd_tx, buf, len_read, &len_write, 1000);
            if (ESP_OK != err) {
                ESP_LOGE(TAG, "i2s write failed:%d", err);
            }
        } else if (ESP_ERR_TIMEOUT == err) {
            continue;
        } else {
            ESP_LOGE(TAG, "i2s read failed:%d", err);
        }
    }
}

void app_main(void) {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(CONFIG_I2S_SAMPLE_BIT, CONFIG_I2S_SAMPLE_CHANNEL),
        .gpio_cfg = {
            .mclk = -1,
            .bclk = CONFIG_GPIO_NUM_MAX98357_BCLK,
            .ws   = CONFIG_GPIO_NUM_MAX98357_LRC,
            .dout = CONFIG_GPIO_NUM_MAX98357_DIN,
            .din  = -1,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    tx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(CONFIG_I2S_SAMPLE_BIT, CONFIG_I2S_SAMPLE_CHANNEL),
        .gpio_cfg = {
            .mclk = -1,
            .bclk = CONFIG_GPIO_NUM_INMP441_SCK,
            .ws   = CONFIG_GPIO_NUM_INMP441_WS,
            .dout = -1,
            .din  = CONFIG_GPIO_NUM_INMP441_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // INMP441 L/R: 0 - left, 1 - right

    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    
    i2s_new_channel(&tx_chan_cfg, &hd_tx, NULL);
    i2s_new_channel(&rx_chan_cfg, NULL, &hd_rx);
    i2s_channel_init_std_mode(hd_tx, &tx_std_cfg);
    i2s_channel_init_std_mode(hd_rx, &rx_std_cfg);
    i2s_channel_enable(hd_tx);
    i2s_channel_enable(hd_rx);
    xTaskCreate(i2s_rw_task, "i2s_rw_task", 4096, NULL, 3, NULL);

    while (1) {
        ESP_LOGI(TAG, "heap size, cur_free:%u min_free:%u largest_free_block:%u",
            heap_caps_get_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL),
            heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL),
            heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}
