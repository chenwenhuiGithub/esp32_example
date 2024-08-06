#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"


static SemaphoreHandle_t mutex_hd = NULL;

static const char *TAG = "freertos_mutex";

static void mutex_task_1(void* parameter)
{
    BaseType_t xReturn = pdTRUE;
  
    while (1) {
        xReturn = xSemaphoreTake(mutex_hd, portMAX_DELAY);
        // xReturn = xSemaphoreTakeRecursive(mutex_hd, portMAX_DELAY);
        if (pdTRUE == xReturn) {
            ESP_LOGI(TAG, "task_1 get mutex ok");
            vTaskDelay(5000/portTICK_PERIOD_MS);
            xReturn = xSemaphoreGive(mutex_hd);
            // xReturn = xSemaphoreGiveRecursive(mutex_hd);
            if (pdTRUE == xReturn) {
                ESP_LOGI(TAG, "task_1 post mutex ok");
            } else {
                ESP_LOGE(TAG, "task_1 post mutex failed");
            }
        } else {
            ESP_LOGE(TAG, "task_1 get mutex timeout");
        }
    }
}

static void mutex_task_2(void* parameter)
{
    BaseType_t xReturn = pdTRUE;
  
    while (1) {
        xReturn = xSemaphoreTake(mutex_hd, 3000/portTICK_PERIOD_MS);
        // xReturn = xSemaphoreTakeRecursive(mutex_hd, 3000/portTICK_PERIOD_MS);
        if (pdTRUE == xReturn) {
            ESP_LOGI(TAG, "task_2 get mutex ok");
            xReturn = xSemaphoreGive(mutex_hd);
            // xReturn = xSemaphoreGiveRecursive(mutex_hd);
            if (pdTRUE == xReturn) {
                ESP_LOGI(TAG, "task_2 post mutex ok");
            } else {
                ESP_LOGE(TAG, "task_2 post mutex failed");
            }
        } else {
            ESP_LOGE(TAG, "task_2 get mutex timeout");
        }
    }
}


void app_main(void)
{
    BaseType_t xReturn = pdPASS;

    mutex_hd = xSemaphoreCreateMutex();
    // mutex_hd = xSemaphoreCreateRecursiveMutex();
    if (!mutex_hd) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex/RecursiveMutex failed");
    }

    xReturn = xTaskCreate(mutex_task_1, "mutex_task_1", 2048, NULL, 2, NULL);
    if (pdPASS != xReturn) {
        ESP_LOGE(TAG, "xTaskCreate mutex_task_1 failed");
    }

    xReturn = xTaskCreate(mutex_task_2, "mutex_task_2", 2048, NULL, 4, NULL);
    if (pdPASS != xReturn) {
        ESP_LOGE(TAG, "xTaskCreate mutex_task_2 failed");
    }

    while (1) {
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}
