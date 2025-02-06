#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_modbus_common.h"
#include "esp_modbus_slave.h"


#define CONFIG_MODBUS_SLAVE_UID             1
#define CONFIG_MODBUS_UART_PORT             UART_NUM_2
#define CONFIG_MODBUS_UART_PIN_RX           16
#define CONFIG_MODBUS_UART_PIN_TX           17
#define CONFIG_MODBUS_UART_BAUD             115200

#pragma pack(push, 1)
typedef struct {
    uint8_t bit_0:1;
    uint8_t bit_1:1;
    uint8_t bit_2:1;
    uint8_t bit_3:1;
    uint8_t bit_4:1;
    uint8_t bit_5:1;
    uint8_t bit_6:1;
    uint8_t bit_7:1;
    uint8_t bit_8:1;
    uint8_t bit_9:1;
    uint8_t bit_10:1;
    uint8_t bit_11:1;
} discrete_input_t;

typedef struct {
    uint8_t bit_0:1;
    uint8_t bit_1:1;
    uint8_t bit_2:1;
    uint8_t bit_3:1;
    uint8_t bit_4:1;
    uint8_t bit_5:1;
    uint8_t bit_6:1;
    uint8_t bit_7:1;
    uint8_t bit_8:1;
    uint8_t bit_9:1;
    uint8_t bit_10:1;
    uint8_t bit_11:1;
    uint8_t bit_12:1;
    uint8_t bit_13:1;
    uint8_t bit_14:1;
    uint8_t bit_15:1;
    uint8_t bit_16:1;
    uint8_t bit_17:1;
    uint8_t bit_18:1;
    uint8_t bit_19:1;
} coil_t;

typedef struct {
    uint16_t reg_0;
    uint16_t reg_1;
    uint16_t reg_2;
} input_reg_t;

typedef struct {
    uint16_t reg_0;
    uint16_t reg_1;
    uint16_t reg_2;
    uint16_t reg_3;
    uint16_t reg_4;
} holding_reg_t;
#pragma pack(pop)

static const char *TAG = "rtu_slave";
static void* hd_rtu_slave = NULL;
static discrete_input_t discrete_input = {0};
static coil_t coil = {0};
static input_reg_t input_reg = {0};
static holding_reg_t holding_reg = {0};


static void init_slave_data() {
    discrete_input.bit_0 = 1;
    discrete_input.bit_5 = 1;
    discrete_input.bit_10 = 1;

    coil.bit_1 = 1;
    coil.bit_6 = 1;
    coil.bit_11 = 1;
    coil.bit_16 = 1;

    input_reg.reg_0 = 0x1234;
    input_reg.reg_1 = 0x5678;
    input_reg.reg_2 = 0xabcd;

    holding_reg.reg_0 = 0x1122;
    holding_reg.reg_1 = 0x3344;
    holding_reg.reg_2 = 0x5566;
    holding_reg.reg_3 = 0x7788;
    holding_reg.reg_4 = 0x9900;
}

static char* get_event_type_string(mb_event_group_t type) {
    char *ret = "unknown type";

    switch (type) {
    case MB_EVENT_DISCRETE_RD:      ret = "read discrete_input";    break;
    case MB_EVENT_COILS_RD:         ret = "read coil";              break;
    case MB_EVENT_COILS_WR:         ret = "write coil";             break;
    case MB_EVENT_INPUT_REG_RD:     ret = "read input_reg";         break;
    case MB_EVENT_HOLDING_REG_RD:   ret = "read holding_reg";       break;
    case MB_EVENT_HOLDING_REG_WR:   ret = "write holding_reg";      break;
    default: break;
    }

    return ret;
}

static void rtu_slave_cb(void *pvParameters) {
    esp_err_t err = ESP_OK;
    mb_communication_info_t comm_info = {0};
    mb_register_area_descriptor_t reg_area = {0};
    mb_param_info_t para_info = {0};

    uart_set_pin(CONFIG_MODBUS_UART_PORT, CONFIG_MODBUS_UART_PIN_TX, CONFIG_MODBUS_UART_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    init_slave_data();

    comm_info.ser_opts.mode = MB_RTU;
    comm_info.ser_opts.port = CONFIG_MODBUS_UART_PORT;
    comm_info.ser_opts.uid = CONFIG_MODBUS_SLAVE_UID;
    comm_info.ser_opts.baudrate = CONFIG_MODBUS_UART_BAUD;
    comm_info.ser_opts.data_bits = UART_DATA_8_BITS;
    comm_info.ser_opts.stop_bits = UART_STOP_BITS_1;
    comm_info.ser_opts.parity = UART_PARITY_DISABLE;
    err = mbc_slave_create_serial(&comm_info, &hd_rtu_slave);
    if ((ESP_OK != err) || (NULL == hd_rtu_slave)) {
        ESP_LOGE(TAG, "mbc_slave_create_serial error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_DISCRETE;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&discrete_input;
    reg_area.size = sizeof(discrete_input);
    err = mbc_slave_set_descriptor(hd_rtu_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_DISCRETE error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&coil;
    reg_area.size = sizeof(coil);
    err = mbc_slave_set_descriptor(hd_rtu_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_COIL error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&input_reg;
    reg_area.size = sizeof(input_reg);
    err = mbc_slave_set_descriptor(hd_rtu_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_INPUT error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&holding_reg;
    reg_area.size = sizeof(holding_reg);
    err = mbc_slave_set_descriptor(hd_rtu_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_HOLDING error:%d", err);
        return;
    }

    err = mbc_slave_start(hd_rtu_slave);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_start error:%d", err);
        return;
    }
    ESP_LOGI(TAG, "mbc_slave_start success");

    while (1) {
        mbc_slave_check_event(hd_rtu_slave,
            MB_EVENT_DISCRETE_RD | MB_EVENT_COILS_RD | MB_EVENT_COILS_WR | MB_EVENT_INPUT_REG_RD | MB_EVENT_HOLDING_REG_RD | MB_EVENT_HOLDING_REG_WR);
        err = mbc_slave_get_param_info(hd_rtu_slave, &para_info, 500);
        if (ESP_OK == err) {
            ESP_LOGI(TAG, "timestamp:%lu offset:%u type:%u(%s) address:%p size:%u",
                para_info.time_stamp, para_info.mb_offset, para_info.type, get_event_type_string(para_info.type), para_info.address, para_info.size);
        }
    }
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

    xTaskCreate(rtu_slave_cb, "rtu_slave", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
