#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"


void app_main(void)
{
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    uint8_t hzline1_1[] = {0, 1, 2};
    uint8_t hzline1_2[] = {3, 4, 5};
    uint8_t hzline2[] = {6, 7, 8, 9, 10, 11};

    oled_init();
    oled_clear();
    oled_show_char(0, 0, 'a', 1);
    oled_show_char(121, 0, 'Z', 1);
    oled_show_string(50, 0, "spi", 1);
    oled_show_string(0, 1, "abcdefg01234567890XYZ", 1);
    oled_show_string(40, 2, "!#$&()?@<>=", 2);
    oled_show_chinese(0, 4, hzline1_1, sizeof(hzline1_1));
    oled_show_chinese(80, 4, hzline1_2, sizeof(hzline1_2));
    oled_show_chinese(16, 6, hzline2, sizeof(hzline2));

    oled_show_point(20, 2, 1);
    oled_show_point(20, 5, 1);
    oled_show_point(100, 2, 1);
    oled_show_point(100, 5, 1);
    oled_show_line(3, 24, 35, 24, 1);
    oled_show_line(20, 20, 20, 30, 1);
    oled_show_line(50, 35, 75, 45, 1);
    oled_show_line(50, 45, 75, 35, 1);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
