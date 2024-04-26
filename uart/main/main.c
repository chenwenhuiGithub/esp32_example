#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#define EXAMPLE_UART_NUM            0

void app_main(void)
{
    uint8_t buf[128] = {0};
    uint32_t len = 0;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(EXAMPLE_UART_NUM, 1024, 0, 0, NULL, 0);
    uart_param_config(EXAMPLE_UART_NUM, &uart_config);
    uart_set_pin(EXAMPLE_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    while (1) {
        len = uart_read_bytes(EXAMPLE_UART_NUM, buf, sizeof(buf), 100 / portTICK_PERIOD_MS);
        if (len) {
            uart_write_bytes(EXAMPLE_UART_NUM, buf, len);
        }
    }
}
