#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"


static TaskHandle_t led_task_hd = NULL;
static TimerHandle_t timer_hd = NULL;

static const char *TAG = "freertos_task";

static void led_task(void* parameter)
{	
    while (1) {
        ESP_LOGI(TAG, "led off"); // gpio_set_level(EXAMPLE_GPIO_LED, 1);  
        vTaskDelay(1000/portTICK_PERIOD_MS);

        ESP_LOGI(TAG, "led on");  // gpio_set_level(EXAMPLE_GPIO_LED, 0); 
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}

static void timer_task(TimerHandle_t xTimer)
{
    BaseType_t xReturn = pdPASS;
    static uint8_t led_task_suspend_flag = 0;

    if (led_task_suspend_flag) {
        vTaskResume(led_task_hd);
        led_task_suspend_flag = 0;
    } else {
        vTaskSuspend(led_task_hd);
        led_task_suspend_flag = 1;
    }
}


void app_main(void)
{
    BaseType_t xReturn = pdPASS;
    uint32_t cur_memory = 0;
    uint32_t min_memory = 0;
    uint8_t * ptr = NULL;

    // components\freertos\config\include\freertos\FreeRTOSConfig.h
    // components\freertos\config\xtensa\include\freertos\FreeRTOSConfig_arch.h
    // components\freertos\FreeRTOS-Kernel\portable\xtensa\include\freertos\portmacro.h
    xReturn = xTaskCreate(led_task, "led_task", 2048, NULL, 2, &led_task_hd); // StackType_t:uint8_t
    if (pdPASS != xReturn) {
        ESP_LOGE(TAG, "xTaskCreate led_task failed");
    }

    timer_hd = xTimerCreate("timer_1", 500, pdTRUE, NULL, timer_task); // 5s, reload
    if (!timer_hd) {
        ESP_LOGE(TAG, "xTimerCreate failed");
    } else {
        xTimerStart(timer_hd, 0);
    }

    while (1) {
        ptr = pvPortMalloc(1024);
        cur_memory = xPortGetFreeHeapSize();
        min_memory = xPortGetMinimumEverFreeHeapSize();
        ESP_LOGI(TAG, "after malloc, cur:%lu min:%lu", cur_memory, min_memory);
        
        vPortFree(ptr);
        cur_memory = xPortGetFreeHeapSize();
        min_memory = xPortGetMinimumEverFreeHeapSize();
        ESP_LOGI(TAG, "after free, cur:%lu min:%lu", cur_memory, min_memory);
        vTaskDelay(10000/portTICK_PERIOD_MS);
    }
}
