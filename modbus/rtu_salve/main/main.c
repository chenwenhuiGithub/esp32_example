#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_modbus_common.h"
#include "esp_modbus_slave.h"


#define EXAMPLE_MODBUS_SLAVE_ADDR    1
#define EXAMPLE_MODBUS_UART_PORT     UART_NUM_2
#define EXAMPLE_MODBUS_UART_PIN_RX   16
#define EXAMPLE_MODBUS_UART_PIN_TX   17
#define EXAMPLE_MODBUS_UART_BAUD     115200

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
} discrete_t;

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
} coils_t;

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

static const char *TAG = "modbus_rtu_slave";
static void* hd_rtu_slave = NULL;
static discrete_t discrete = {0};
static coils_t coils = {0};
static input_reg_t input_reg = {0};
static holding_reg_t holding_reg = {0};


static void init_slave_data(void) {
    discrete.bit_0 = 1;
    discrete.bit_2 = 1;
    discrete.bit_4 = 1;
    discrete.bit_6 = 1;
    discrete.bit_8 = 1;
    discrete.bit_10 = 1;

    coils.bit_1 = 1;
    coils.bit_3 = 1;
    coils.bit_5 = 1;
    coils.bit_7 = 1;
    coils.bit_9 = 1;
    coils.bit_11 = 1;
    coils.bit_13 = 1;
    coils.bit_15 = 1;
    coils.bit_17 = 1;
    coils.bit_19 = 1;

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
    case MB_EVENT_DISCRETE_RD:      ret = "discrete read";      break;
    case MB_EVENT_COILS_RD:         ret = "coils read";         break;
    case MB_EVENT_COILS_WR:         ret = "coils write";        break;
    case MB_EVENT_INPUT_REG_RD:     ret = "input_reg read";     break;
    case MB_EVENT_HOLDING_REG_RD:   ret = "holding_reg read";   break;
    case MB_EVENT_HOLDING_REG_WR:   ret = "holding_reg write";  break;
    default: break;
    }
    return ret;
}

static void rtu_slave_task(void *pvParameters) {
    esp_err_t err = ESP_OK;
    mb_communication_info_t comm_info = {0};
    mb_register_area_descriptor_t reg_area = {0};
    mb_param_info_t para_info = {0};

    init_slave_data();

    err = mbc_slave_init(MB_PORT_SERIAL_SLAVE, &hd_rtu_slave);
    if ((ESP_OK != err) || (hd_rtu_slave == NULL)) {
        ESP_LOGE(TAG, "mbc_slave_init error:%d", err);
        return;
    }

    comm_info.mode = MB_MODE_RTU;
    comm_info.slave_addr = EXAMPLE_MODBUS_SLAVE_ADDR;
    comm_info.port = EXAMPLE_MODBUS_UART_PORT;
    comm_info.baudrate = EXAMPLE_MODBUS_UART_BAUD;
    comm_info.parity = UART_PARITY_DISABLE;
    err = mbc_slave_setup((void*)&comm_info);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_setup error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_DISCRETE;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&discrete;
    reg_area.size = sizeof(discrete);
    err = mbc_slave_set_descriptor(reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_DISCRETE error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&coils;
    reg_area.size = sizeof(coils);
    err = mbc_slave_set_descriptor(reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_COIL error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&input_reg;
    reg_area.size = sizeof(input_reg);
    err = mbc_slave_set_descriptor(reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_INPUT error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&holding_reg;
    reg_area.size = sizeof(holding_reg);
    err = mbc_slave_set_descriptor(reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_HOLDING error:%d", err);
        return;
    }

    err = mbc_slave_start();
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_start error:%d", err);
        return;
    }

    ESP_LOGI(TAG, "mbc_slave_start ok");

    uart_set_pin(EXAMPLE_MODBUS_UART_PORT, EXAMPLE_MODBUS_UART_PIN_TX, EXAMPLE_MODBUS_UART_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    while (1) {
        mbc_slave_check_event(MB_EVENT_DISCRETE_RD | MB_EVENT_COILS_RD | MB_EVENT_COILS_WR |
                              MB_EVENT_INPUT_REG_RD | MB_EVENT_HOLDING_REG_RD | MB_EVENT_HOLDING_REG_WR);
        err = mbc_slave_get_param_info(&para_info, 100);
        if (ESP_OK == err) {
            ESP_LOGI(TAG, "recv req, timestamp:%lu offset:%u type:%u(%s) address:%p size:%u",
                para_info.time_stamp, para_info.mb_offset, para_info.type, get_event_type_string(para_info.type), para_info.address, para_info.size);
        }
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

    xTaskCreate(rtu_slave_task, "rtu_slave_task", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
