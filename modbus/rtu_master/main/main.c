#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_modbus_common.h"
#include "esp_modbus_master.h"


#define EXAMPLE_MODBUS_SLAVE_ADDR               1
#define EXAMPLE_MODBUS_UART_PORT                UART_NUM_2
#define EXAMPLE_MODBUS_UART_PIN_RX              16
#define EXAMPLE_MODBUS_UART_PIN_TX              17
#define EXAMPLE_MODBUS_UART_BAUD                115200

#define FUNC_CODE_READ_COILS                    0x01
#define FUNC_CODE_READ_DISCRETE                 0x02
#define FUNC_CODE_READ_HOLDING_REG              0x03
#define FUNC_CODE_READ_INPUT_REG                0x04
#define FUNC_CODE_WRITE_SINGLE_COILS            0x05
#define FUNC_CODE_WRITE_SINGLE_HOLDING_REG      0x06
#define FUNC_CODE_WRITE_MULTIPLE_COILS          0x0f
#define FUNC_CODE_WRITE_MULTIPLE_HOLDING_REG    0x10

static const char *TAG = "modbus_rtu_master";
static void* hd_rtu_master = NULL;

static void rtu_master_task(void *pvParameters) {
    esp_err_t err = ESP_OK;
    mb_communication_info_t comm_info = {0};
    mb_param_request_t param_req = {0};
    uint8_t read_discrete = 0;
    uint8_t read_coils[2] = {0};
    uint16_t read_input_reg = 0;
    uint16_t read_holding_reg[3] = {0};
    uint16_t write_single_coils = 0xff00; // 0xff00 - 1, 0x0000 - 0
    uint16_t write_single_holding_reg = 0xa55a;
    uint8_t write_multi_coils[2] = {0x0f, 0xff};
    uint16_t write_multi_holding_reg[3] = {0x11ff, 0x22ee, 0x33dd};

    err = mbc_master_init(MB_PORT_SERIAL_MASTER, &hd_rtu_master);
    if ((ESP_OK != err) || (hd_rtu_master == NULL)) {
        ESP_LOGE(TAG, "mbc_master_init error:%d", err);
        return;
    }

    comm_info.mode = MB_MODE_RTU;
    comm_info.slave_addr = EXAMPLE_MODBUS_SLAVE_ADDR;
    comm_info.port = EXAMPLE_MODBUS_UART_PORT;
    comm_info.baudrate = EXAMPLE_MODBUS_UART_BAUD;
    comm_info.parity = UART_PARITY_DISABLE;
    err = mbc_master_setup((void*)&comm_info);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_master_setup error:%d", err);
        return;
    }

    err = mbc_master_start();
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_master_start error:%d", err);
        return;
    }

    ESP_LOGI(TAG, "mbc_master_start ok");

    uart_set_pin(EXAMPLE_MODBUS_UART_PORT, EXAMPLE_MODBUS_UART_PIN_TX, EXAMPLE_MODBUS_UART_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    while (1) {
        param_req.slave_addr = EXAMPLE_MODBUS_SLAVE_ADDR;

        ESP_LOGI(TAG, "read discrete bit_5");
        param_req.command = FUNC_CODE_READ_DISCRETE;
        param_req.reg_start = 5;
        param_req.reg_size = 1;
        mbc_master_send_request(&param_req, &read_discrete);
        ESP_LOGI(TAG, "%d", read_discrete & 0x01);

        ESP_LOGI(TAG, "read coils bit_4..15");
        param_req.command = FUNC_CODE_READ_COILS;
        param_req.reg_start = 4;
        param_req.reg_size = 12;
        mbc_master_send_request(&param_req, read_coils);
        ESP_LOGI(TAG, "[15 14 13 12][11 10 9 8 7 6 5 4]");
        ESP_LOGI(TAG, "[%d %d %d %d][%d %d %d %d %d %d %d %d]",
            read_coils[1] & 0x08, read_coils[1] & 0x04, read_coils[1] & 0x02, read_coils[1] & 0x01,
            read_coils[0] & 0x80, read_coils[0] & 0x40, read_coils[0] & 0x20, read_coils[0] & 0x10,
            read_coils[0] & 0x08, read_coils[0] & 0x04, read_coils[0] & 0x02, read_coils[0] & 0x01);

        ESP_LOGI(TAG, "read input_reg reg_1");
        param_req.command = FUNC_CODE_READ_INPUT_REG;
        param_req.reg_start = 1;
        param_req.reg_size = 1;
        mbc_master_send_request(&param_req, &read_input_reg);
        ESP_LOGI(TAG, "0x%04x", read_input_reg);

        ESP_LOGI(TAG, "read holding_reg reg_1..3");
        param_req.command = FUNC_CODE_READ_HOLDING_REG;
        param_req.reg_start = 1;
        param_req.reg_size = 3;
        mbc_master_send_request(&param_req, read_holding_reg);
        ESP_LOGI(TAG, "[3][2][1]");
        ESP_LOGI(TAG, "[0x%04x][0x%04x][0x%04x]", read_holding_reg[2], read_holding_reg[1], read_holding_reg[0]);

        ESP_LOGI(TAG, "write single coils bit_10");
        param_req.command = FUNC_CODE_WRITE_SINGLE_COILS;
        param_req.reg_start = 10;
        param_req.reg_size = 1;
        mbc_master_send_request(&param_req, &write_single_coils);

        ESP_LOGI(TAG, "write single holding_reg reg_2");
        param_req.command = FUNC_CODE_WRITE_SINGLE_HOLDING_REG;
        param_req.reg_start = 2;
        param_req.reg_size = 1;
        mbc_master_send_request(&param_req, &write_single_holding_reg);

        ESP_LOGI(TAG, "write multiple coils bit_4_15");
        param_req.command = FUNC_CODE_WRITE_MULTIPLE_COILS;
        param_req.reg_start = 4;
        param_req.reg_size = 12;
        mbc_master_send_request(&param_req, &write_multi_coils);

        ESP_LOGI(TAG, "write multiple holding_reg reg_1_3");
        param_req.command = FUNC_CODE_WRITE_MULTIPLE_HOLDING_REG;
        param_req.reg_start = 1;
        param_req.reg_size = 3;
        mbc_master_send_request(&param_req, &write_multi_holding_reg);

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t err = ESP_OK;
    
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_flash_init error:%d", err);
        return;
    }

    xTaskCreate(rtu_master_task, "rtu_master_task", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
