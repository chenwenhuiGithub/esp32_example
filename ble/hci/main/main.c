#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "soc/uhci_periph.h"
#include "esp_private/periph_ctrl.h" 
#include "driver/uart.h"


#define TAG                             "ble_hci"
#define EXAMPLE_HCI_UART_NUM            0


void app_main(void)
{
    esp_err_t err = ESP_OK;
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_flash_init error:%d", err);
        return;
    }

    periph_module_enable(PERIPH_UHCI0_MODULE);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.hci_uart_no = EXAMPLE_HCI_UART_NUM; // 由于 UART0 默认是日志输出，因此 menuconfig 只能配置 hci 到 UART1/UART2，此处强制配置到 UART0
    esp_bt_controller_init(&bt_cfg);

    uart_driver_delete(EXAMPLE_HCI_UART_NUM);
    uart_driver_install(EXAMPLE_HCI_UART_NUM, 1024, 0, 0, NULL, 0);
    uart_param_config(EXAMPLE_HCI_UART_NUM, &uart_config);
    uart_set_pin(EXAMPLE_HCI_UART_NUM, 1, 3, -1, -1);
    uart_set_hw_flow_ctrl(EXAMPLE_HCI_UART_NUM, UART_HW_FLOWCTRL_DISABLE, UART_FIFO_LEN - 8);

    esp_bt_controller_enable(ESP_BT_MODE_BLE);
}
