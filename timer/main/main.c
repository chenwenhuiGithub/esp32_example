#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/timer.h"
#include "driver/gpio.h"


#define EXAMPLE_GPIO_LED                5
#define EXAMPLE_TIMER_DIVIDER           16  //  Hardware timer clock divider
#define EXAMPLE_TIMER_VALUE_SEC         (TIMER_BASE_CLK / 16)  // convert counter value to seconds


static bool IRAM_ATTR timer_group_isr_callback(void *args)
{
    static uint8_t onoff = 0;

    if (onoff) {
        gpio_set_level(EXAMPLE_GPIO_LED, 1);
        onoff = 0;
    } else {
        gpio_set_level(EXAMPLE_GPIO_LED, 0);
        onoff = 1;        
    }
    return pdTRUE;
}

void app_main(void)
{
    gpio_config_t io_conf = { 0 };
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << EXAMPLE_GPIO_LED;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    gpio_set_level(EXAMPLE_GPIO_LED, 0); // 0 - off, 1 - on

    timer_config_t timer_conf = {
        .divider = EXAMPLE_TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true,
    }; // default clock source is APB
    timer_init(TIMER_GROUP_0, TIMER_0, &timer_conf);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 1 * EXAMPLE_TIMER_VALUE_SEC); // period = 1s
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_group_isr_callback, NULL, 0);
    timer_start(TIMER_GROUP_0, TIMER_0);

    while (1) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
