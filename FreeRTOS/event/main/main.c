#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"


#define KEY1_EVENT                      (0x01 << 0)
#define KEY2_EVENT                      (0x01 << 1)

static TimerHandle_t timer_hd = NULL;
static EventGroupHandle_t evt_hd = NULL;

static const char *TAG = "freertos_event";

static void evt_recv_task(void* parameter)
{
    EventBits_t evt_bits = 0;
  
    while (1) {
        evt_bits = xEventGroupWaitBits(evt_hd, KEY1_EVENT|KEY2_EVENT, pdTRUE, pdTRUE, 3000/portTICK_PERIOD_MS); // clear bits on exit, wait all bits
        ESP_LOGI(TAG, "evt_bits:0x%04x", (unsigned int)evt_bits);
        if ((evt_bits & (KEY1_EVENT|KEY2_EVENT)) == (KEY1_EVENT|KEY2_EVENT)) {
            ESP_LOGI(TAG, "both KEY1_EVENT and KEY2_EVENT occured");
        }
    }
}

static void timer_task(TimerHandle_t xTimer)
{
    BaseType_t xReturn = pdPASS;
    static uint8_t evt_set_bits_flag = 0;

    if (evt_set_bits_flag) {
        xEventGroupSetBits(evt_hd, KEY1_EVENT);
        evt_set_bits_flag = 0;
    } else {
        xEventGroupSetBits(evt_hd, KEY2_EVENT);
        evt_set_bits_flag = 1;
    }
}


void app_main(void)
{
    BaseType_t xReturn = pdPASS;

    evt_hd = xEventGroupCreate();
    if (!evt_hd) {
        ESP_LOGE(TAG, "xEventGroupCreate failed");
    }

    xReturn = xTaskCreate(evt_recv_task, "evt_recv_task", 2048, NULL, 6, NULL);
    if (pdPASS != xReturn) {
        ESP_LOGE(TAG, "xTaskCreate evt_recv_task failed");
    }

    timer_hd = xTimerCreate("timer_1", 500, pdTRUE, NULL, timer_task); // 5s, reload
    if (!timer_hd) {
        ESP_LOGE(TAG, "xTimerCreate failed");
    } else {
        xTimerStart(timer_hd, 0);
    }

    while (1) {
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}
