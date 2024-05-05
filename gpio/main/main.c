#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define EXAMPLE_GPIO_LED            5
#define EXAMPLE_GPIO_ONOFF          4

static bool g_onoff = true;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    if (g_onoff) {
        g_onoff = false;
    } else {
        g_onoff = true;
    }
}

void app_main(void)
{
    gpio_config_t conf = { 0 };

    conf.intr_type = GPIO_INTR_DISABLE;
    conf.pin_bit_mask = 1ULL << EXAMPLE_GPIO_LED;
    conf.mode = GPIO_MODE_OUTPUT;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&conf);

    conf.intr_type = GPIO_INTR_NEGEDGE;
    conf.pin_bit_mask = 1ULL << EXAMPLE_GPIO_ONOFF;
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(EXAMPLE_GPIO_ONOFF, gpio_isr_handler, NULL);

    gpio_set_level(EXAMPLE_GPIO_LED, 0); // 0 - off, 1 - on
    while (1) {
        if (g_onoff) {
            gpio_set_level(EXAMPLE_GPIO_LED, 1);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            gpio_set_level(EXAMPLE_GPIO_LED, 0);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        } else {
            gpio_set_level(EXAMPLE_GPIO_LED, 0);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
}
