#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"


static TimerHandle_t timer_hd = NULL;
static SemaphoreHandle_t sem_hd = NULL;

static const char *TAG = "freertos_sem";

static void sem_recv_task(void* parameter)
{
    BaseType_t xReturn = pdTRUE;
  
    while (1) {
        xReturn = xSemaphoreTake(sem_hd, 3000/portTICK_PERIOD_MS);
        if (pdTRUE == xReturn) {
            ESP_LOGI(TAG, "xSemaphoreTake sem ok");
        } else {
            ESP_LOGE(TAG, "xSemaphoreTake sem timeout");
        }
    }
}

static void timer_task(TimerHandle_t xTimer)
{
    BaseType_t xReturn = pdPASS;

    xReturn = xSemaphoreGive(sem_hd);
    if(pdPASS != xReturn) {
        ESP_LOGE(TAG, "xSemaphoreGive sem failed");
    }
}


void app_main(void)
{
    BaseType_t xReturn = pdPASS;

    sem_hd = xSemaphoreCreateCounting(10, 0);
    // sem_hd = xSemaphoreCreateBinary();
    if (!sem_hd) {
        ESP_LOGE(TAG, "xSemaphoreCreateCounting/Binary failed");
    }

    xReturn = xTaskCreate(sem_recv_task, "sem_recv_task", 2048, NULL, 4, NULL);
    if (pdPASS != xReturn) {
        ESP_LOGE(TAG, "xTaskCreate sem_recv_task failed");
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
