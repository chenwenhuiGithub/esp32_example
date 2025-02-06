#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_modbus_common.h"
#include "esp_modbus_master.h"


#define CONFIG_MODBUS_SLAVE_UID                 1
#define CONFIG_MODBUS_UART_PORT                 UART_NUM_2
#define CONFIG_MODBUS_UART_PIN_RX               16
#define CONFIG_MODBUS_UART_PIN_TX               17
#define CONFIG_MODBUS_UART_BAUD                 115200

#define CMD_READ_COIL                           0x01
#define CMD_READ_DISCRETE_INPUT                 0x02
#define CMD_READ_HOLDING_REG                    0x03
#define CMD_READ_INPUT_REG                      0x04
#define CMD_WRITE_SINGLE_COIL                   0x05
#define CMD_WRITE_SINGLE_HOLDING_REG            0x06
#define CMD_WRITE_MULTIPLE_COIL                 0x0f
#define CMD_WRITE_MULTIPLE_HOLDING_REG          0x10

static const char *TAG = "rtu_master";
static void* hd_rtu_master = NULL;

static void read_discrete_input() {
    mb_param_request_t param_req = {0};
    uint8_t data[2] = {0};

    param_req.slave_addr = CONFIG_MODBUS_SLAVE_UID;
    param_req.command = CMD_READ_DISCRETE_INPUT;

    ESP_LOGI(TAG, "read discrete_input bit_1");
    param_req.reg_start = 1;
    param_req.reg_size = 1;
    mbc_master_send_request(hd_rtu_master, &param_req, &data[0]);
    ESP_LOGI(TAG, "%u", data[0] & 0x01);

    ESP_LOGI(TAG, "read discrete_input bit_12..3");
    param_req.reg_start = 3;
    param_req.reg_size = 10;
    mbc_master_send_request(hd_rtu_master, &param_req, data);
    ESP_LOGI(TAG, "[12 11][10 9 8 7 6 5 4 3]");
    ESP_LOGI(TAG, "[%u %u][%u %u %u %u %u %u %u %u]",
        data[1] & 0x02, data[1] & 0x01, data[0] & 0x80, data[0] & 0x40, data[0] & 0x20,
        data[0] & 0x10, data[0] & 0x08, data[0] & 0x04, data[0] & 0x02, data[0] & 0x01);
}

static void read_coil() {
    mb_param_request_t param_req = {0};
    uint8_t data[2] = {0};

    param_req.slave_addr = CONFIG_MODBUS_SLAVE_UID;
    param_req.command = CMD_READ_COIL;

    ESP_LOGI(TAG, "read coil bit_2");
    param_req.reg_start = 2;
    param_req.reg_size = 1;
    mbc_master_send_request(hd_rtu_master, &param_req, &data[0]);
    ESP_LOGI(TAG, "%u", data[0] & 0x01);

    ESP_LOGI(TAG, "read coil bit_14..5");
    param_req.reg_start = 5;
    param_req.reg_size = 10;
    mbc_master_send_request(hd_rtu_master, &param_req, data);
    ESP_LOGI(TAG, "[14 13][12 11 10 9 8 7 6 5]");
    ESP_LOGI(TAG, "[%u %u][%u %u %u %u %u %u %u %u]",
        data[1] & 0x02, data[1] & 0x01, data[0] & 0x80, data[0] & 0x40, data[0] & 0x20,
        data[0] & 0x10, data[0] & 0x08, data[0] & 0x04, data[0] & 0x02, data[0] & 0x01);
}

static void write_coil() {
    mb_param_request_t param_req = {0};
    uint8_t single_data[2] = {0xff, 0x00}; // 0xff00 - 1, 0x0000 - 0
    uint8_t mult_data[2] = {0x03, 0xbd};

    param_req.slave_addr = CONFIG_MODBUS_SLAVE_UID;

    ESP_LOGI(TAG, "write coil bit_2");
    param_req.command = CMD_WRITE_SINGLE_COIL;
    param_req.reg_start = 2;
    param_req.reg_size = 1;
    mbc_master_send_request(hd_rtu_master, &param_req, single_data);
    ESP_LOGI(TAG, "%u", 1);

    ESP_LOGI(TAG, "write coil bit_14..5");
    param_req.command = CMD_WRITE_MULTIPLE_COIL;
    param_req.reg_start = 5;
    param_req.reg_size = 10;
    mbc_master_send_request(hd_rtu_master, &param_req, mult_data);
    ESP_LOGI(TAG, "[14 13][12 11 10 9 8 7 6 5]");
    ESP_LOGI(TAG, "[%u %u][%u %u %u %u %u %u %u %u]",
        mult_data[1] & 0x02, mult_data[1] & 0x01, mult_data[0] & 0x80, mult_data[0] & 0x40, mult_data[0] & 0x20,
        mult_data[0] & 0x10, mult_data[0] & 0x08, mult_data[0] & 0x04, mult_data[0] & 0x02, mult_data[0] & 0x01);
}

static void read_input_reg() {
    mb_param_request_t param_req = {0};
    uint16_t data[2] = {0};

    param_req.slave_addr = CONFIG_MODBUS_SLAVE_UID;
    param_req.command = CMD_READ_INPUT_REG;

    ESP_LOGI(TAG, "read input_reg reg_0");
    param_req.reg_start = 0;
    param_req.reg_size = 1;
    mbc_master_send_request(hd_rtu_master, &param_req, &data[0]);
    ESP_LOGI(TAG, "0x%04x", data[0]);

    ESP_LOGI(TAG, "read input_reg reg_2..1");
    param_req.reg_start = 1;
    param_req.reg_size = 2;
    mbc_master_send_request(hd_rtu_master, &param_req, data);
    ESP_LOGI(TAG, "[2]:0x%04x [1]:0x%04x", data[1], data[0]);
}

static void read_holding_reg() {
    mb_param_request_t param_req = {0};
    uint16_t data[3] = {0};

    param_req.slave_addr = CONFIG_MODBUS_SLAVE_UID;
    param_req.command = CMD_READ_HOLDING_REG;

    ESP_LOGI(TAG, "read holding_reg reg_1");
    param_req.reg_start = 1;
    param_req.reg_size = 1;
    mbc_master_send_request(hd_rtu_master, &param_req, &data[0]);
    ESP_LOGI(TAG, "0x%04x", data[0]);

    ESP_LOGI(TAG, "read holding_reg reg_4..2");
    param_req.reg_start = 2;
    param_req.reg_size = 3;
    mbc_master_send_request(hd_rtu_master, &param_req, data);
    ESP_LOGI(TAG, "[4]:0x%04x [3]:0x%04x [2]:0x%04x", data[2], data[1], data[0]);
}

static void write_holding_reg() {
    mb_param_request_t param_req = {0};
    uint16_t single_data = 0xa55a;
    uint16_t mult_data[3] = {0x11ff, 0x22ee, 0x33dd};

    param_req.slave_addr = CONFIG_MODBUS_SLAVE_UID;

    ESP_LOGI(TAG, "write holding_reg reg_1");
    param_req.command = CMD_WRITE_SINGLE_HOLDING_REG;
    param_req.reg_start = 1;
    param_req.reg_size = 1;
    mbc_master_send_request(hd_rtu_master, &param_req, &single_data);
    ESP_LOGI(TAG, "0x%04x", single_data);

    ESP_LOGI(TAG, "write holding_reg reg_4..2");
    param_req.command = CMD_WRITE_MULTIPLE_HOLDING_REG;
    param_req.reg_start = 2;
    param_req.reg_size = 3;
    mbc_master_send_request(hd_rtu_master, &param_req, mult_data);
    ESP_LOGI(TAG, "[4]:0x%04x [3]:0x%04x [2]:0x%04x", mult_data[2], mult_data[1], mult_data[0]);
}

static void rtu_master() {
    esp_err_t err = ESP_OK;
    mb_communication_info_t comm_info = {0};

    uart_set_pin(CONFIG_MODBUS_UART_PORT, CONFIG_MODBUS_UART_PIN_TX, CONFIG_MODBUS_UART_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    comm_info.ser_opts.mode = MB_RTU;
    comm_info.ser_opts.port = CONFIG_MODBUS_UART_PORT;
    comm_info.ser_opts.uid = CONFIG_MODBUS_SLAVE_UID;
    comm_info.ser_opts.baudrate = CONFIG_MODBUS_UART_BAUD;
    comm_info.ser_opts.data_bits = UART_DATA_8_BITS;
    comm_info.ser_opts.stop_bits = UART_STOP_BITS_1;
    comm_info.ser_opts.parity = UART_PARITY_DISABLE;
    err = mbc_master_create_serial(&comm_info, &hd_rtu_master);
    if ((ESP_OK != err) || (NULL == hd_rtu_master)) {
        ESP_LOGE(TAG, "mbc_master_create_serial error:%d", err);
        return;
    }

    err = mbc_master_start(hd_rtu_master);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_master_start error:%d", err);
        return;
    }
    ESP_LOGI(TAG, "mbc_master_start success");

    read_discrete_input();
    vTaskDelay(pdMS_TO_TICKS(500));

    read_coil();
    vTaskDelay(pdMS_TO_TICKS(500));
    write_coil();
    vTaskDelay(pdMS_TO_TICKS(500));
    read_coil();
    vTaskDelay(pdMS_TO_TICKS(500));

    read_input_reg();
    vTaskDelay(pdMS_TO_TICKS(500));

    read_holding_reg();
    vTaskDelay(pdMS_TO_TICKS(500));
    write_holding_reg();
    vTaskDelay(pdMS_TO_TICKS(500));
    read_holding_reg();
    vTaskDelay(pdMS_TO_TICKS(500));
}

void app_main(void)
{
    esp_err_t err = ESP_OK;
    
    err = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == err || ESP_ERR_NVS_NEW_VERSION_FOUND == err) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_flash_init error:%d", err);
        return;
    }

    rtu_master();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
