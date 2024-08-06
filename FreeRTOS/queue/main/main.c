#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"



static TimerHandle_t timer_hd = NULL;
static QueueHandle_t queue_hd = NULL;

static const char *TAG = "freertos_queue";

static void queue_recv_task(void* parameter)
{
    BaseType_t xReturn = pdTRUE;
    uint32_t recv_queue_data = 0;
  
    while (1) {
        xReturn = xQueueReceive(queue_hd, &recv_queue_data, portMAX_DELAY);
        if (pdTRUE == xReturn) {
            ESP_LOGI(TAG, "xQueueReceive ok:%ld", recv_queue_data);
        } else {
            ESP_LOGE(TAG, "xQueueReceive failed");
        }
    }
}

static void timer_task(TimerHandle_t xTimer)
{
    BaseType_t xReturn = pdPASS;
    static uint32_t send_queue_data = 0;

    send_queue_data++;
    xReturn = xQueueSend(queue_hd, &send_queue_data, 0); // value copy mode
    if(pdPASS != xReturn) {
        ESP_LOGE(TAG, "xQueueSend failed");
    }
}


void app_main(void)
{
    BaseType_t xReturn = pdPASS;

    queue_hd = xQueueCreate(10, sizeof(uint32_t));
    if (!queue_hd) {
        ESP_LOGE(TAG, "xQueueCreate failed");
    }

    xReturn = xTaskCreate(queue_recv_task, "queue_recv_task", 2048, NULL, 3, NULL);
    if (pdPASS != xReturn) {
        ESP_LOGE(TAG, "xTaskCreate queue_recv_task failed");
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
