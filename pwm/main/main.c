#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"


#define EXAMPLE_LEDC_GPIO_R                 5
#define EXAMPLE_LEDC_GPIO_G                 18
#define EXAMPLE_LEDC_GPIO_B                 19
#define EXAMPLE_LEDC_TIMER                  LEDC_TIMER_0
#define EXAMPLE_LEDC_DUTY_RESOLUTION        LEDC_TIMER_12_BIT // 0x0000 - 0%, 0x0FFF - 100%
#define EXAMPLE_LEDC_DUTY_FULL              (1 << EXAMPLE_LEDC_DUTY_RESOLUTION)  // 100%
#define EXAMPLE_LEDC_DUTY_HIGH              (EXAMPLE_LEDC_DUTY_FULL * 0.9)       // 90%
#define EXAMPLE_LEDC_DUTY_LOW               (EXAMPLE_LEDC_DUTY_FULL * 0.1)       // 10%
#define EXAMPLE_LEDC_FREQ                   5000 // Hz
#define EXAMPLE_LEDC_FADE_TIME              2000 // ms


void app_main(void)
{
    uint32_t i = 0;
    ledc_timer_config_t timer = {
        .duty_resolution = EXAMPLE_LEDC_DUTY_RESOLUTION,
        .freq_hz = EXAMPLE_LEDC_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = EXAMPLE_LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channels[3] = {
        {
            .channel    = LEDC_CHANNEL_0,
            .duty       = 0,
            .gpio_num   = EXAMPLE_LEDC_GPIO_R,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = EXAMPLE_LEDC_TIMER
        },
        {
            .channel    = LEDC_CHANNEL_1,
            .duty       = 0,
            .gpio_num   = EXAMPLE_LEDC_GPIO_G,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = EXAMPLE_LEDC_TIMER
        },
        {
            .channel    = LEDC_CHANNEL_2,
            .duty       = 0,
            .gpio_num   = EXAMPLE_LEDC_GPIO_B,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = EXAMPLE_LEDC_TIMER
        }
    };

    ledc_timer_config(&timer);
    for (i = 0; i < 3; i++) {
        ledc_channel_config(&channels[i]);
    }
    ledc_fade_func_install(0);

    while (1) {
        for (i = 0; i < 3; i++) {
            ledc_set_fade_with_time(channels[i].speed_mode, channels[i].channel, EXAMPLE_LEDC_DUTY_HIGH, EXAMPLE_LEDC_FADE_TIME);
            ledc_fade_start(channels[i].speed_mode, channels[i].channel, LEDC_FADE_WAIT_DONE); // 0% ~ 90%

            ledc_set_duty(channels[i].speed_mode, channels[i].channel, EXAMPLE_LEDC_DUTY_FULL);
            ledc_update_duty(channels[i].speed_mode, channels[i].channel); // 100%
            vTaskDelay(500 / portTICK_PERIOD_MS);

            ledc_set_fade_with_time(channels[i].speed_mode, channels[i].channel, EXAMPLE_LEDC_DUTY_LOW, EXAMPLE_LEDC_FADE_TIME);
            ledc_fade_start(channels[i].speed_mode, channels[i].channel, LEDC_FADE_WAIT_DONE); // 100% ~ 10%

            ledc_set_duty(channels[i].speed_mode, channels[i].channel, 0);
            ledc_update_duty(channels[i].speed_mode, channels[i].channel); // 0%
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }     
    }
}
