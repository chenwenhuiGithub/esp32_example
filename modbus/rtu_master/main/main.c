#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define CONFIG_MODBUS_SLAVE_UID             1
#define CONFIG_MODBUS_UART_PORT             UART_NUM_2
#define CONFIG_MODBUS_UART_PIN_RX           16
#define CONFIG_MODBUS_UART_PIN_TX           17
#define CONFIG_MODBUS_UART_BAUD             115200
#define CONFIG_MODBUS_RECV_TIMEOUT_MS       1000

#define MODBUS_CMD_READ_COIL                0x01
#define MODBUS_CMD_READ_DISCRETE            0x02
#define MODBUS_CMD_READ_HOLDING             0x03
#define MODBUS_CMD_READ_INPUT               0x04
#define MODBUS_CMD_WRITE_SINGLE_COIL        0x05
#define MODBUS_CMD_WRITE_SINGLE_HOLDING     0x06
#define MODBUS_CMD_WRITE_MULTIPLE_COIL      0x0f
#define MODBUS_CMD_WRITE_MULTIPLE_HOLDING   0x10

static const char *TAG = "rtu_master";

static uint16_t calc_crc16(uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    uint16_t i = 0, j = 0;

    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint8_t check_crc16(uint8_t *data, uint16_t len) {
    uint16_t crc_calc = 0, crc_recv = 0;

    crc_calc = calc_crc16(data, len - 2);
    crc_recv = (data[len - 1] << 8) | data[len -2];
    if (crc_calc != crc_recv) {
        return 1;
    }
    return 0;
}

static void process_read_coil() {
    uint8_t req[8] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_READ_COIL;

    ESP_LOGI(TAG, "read coil bit_1");
    req[2] = 0;
    req[3] = 1; // start_addr
    req[4] = 0;
    req[5] = 1; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "%u", resp[3] & 0x01);                
            }
        }
    }

    ESP_LOGI(TAG, "read coil bit_14..5");
    req[2] = 0;
    req[3] = 5; // start_addr
    req[4] = 0;
    req[5] = 10; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "[14 13][12 11 10 9 8 7 6 5]");
                ESP_LOGI(TAG, "[ %u  %u][ %u  %u  %u %u %u %u %u %u]",
                    (resp[4] & 0x02) ? 1 : 0,
                    resp[4] & 0x01,
                    (resp[3] & 0x80) ? 1 : 0,
                    (resp[3] & 0x40) ? 1 : 0,
                    (resp[3] & 0x20) ? 1 : 0,
                    (resp[3] & 0x10) ? 1 : 0,
                    (resp[3] & 0x08) ? 1 : 0,
                    (resp[3] & 0x04) ? 1 : 0,
                    (resp[3] & 0x02) ? 1 : 0,
                    resp[3] & 0x01);
            }
        }
    }
}

static void process_read_discrete() {
    uint8_t req[8] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_READ_DISCRETE;

    ESP_LOGI(TAG, "read discrete bit_0");
    req[2] = 0;
    req[3] = 0; // start_addr
    req[4] = 0;
    req[5] = 1; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "%u", resp[3] & 0x01);
            }
        }
    }

    ESP_LOGI(TAG, "read discrete bit_9..1");
    req[2] = 0;
    req[3] = 1; // start_addr
    req[4] = 0;
    req[5] = 9; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "[9][8 7 6 5 4 3 2 1]");
                ESP_LOGI(TAG, "[%u][%u %u %u %u %u %u %u %u]",
                    resp[4] & 0x01,
                    (resp[3] & 0x80) ? 1 : 0,
                    (resp[3] & 0x40) ? 1 : 0,
                    (resp[3] & 0x20) ? 1 : 0,
                    (resp[3] & 0x10) ? 1 : 0,
                    (resp[3] & 0x08) ? 1 : 0,
                    (resp[3] & 0x04) ? 1 : 0,
                    (resp[3] & 0x02) ? 1 : 0,
                    resp[3] & 0x01);
            }
        }
    }
}

static void process_read_holding() {
    uint8_t req[8] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_READ_HOLDING;

    ESP_LOGI(TAG, "read holding reg_1");
    req[2] = 0;
    req[3] = 1; // start_addr
    req[4] = 0;
    req[5] = 1; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "0x%04x", (resp[3] << 8) | resp[4]);
            }
        }
    }

    ESP_LOGI(TAG, "read holding reg_4..2");
    req[2] = 0;
    req[3] = 2; // start_addr
    req[4] = 0;
    req[5] = 3; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "[4]:0x%04x [3]:0x%04x [2]:0x%04x",
                    (resp[7] << 8) | resp[8],
                    (resp[5] << 8) | resp[6],
                    (resp[3] << 8) | resp[4]);
            }
        }
    }
}

static void process_read_input() {
    uint8_t req[8] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_READ_INPUT;

    ESP_LOGI(TAG, "read input reg_0");
    req[2] = 0;
    req[3] = 0; // start_addr
    req[4] = 0;
    req[5] = 1; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "0x%04x", (resp[3] << 8) | resp[4]);
            }
        }
    }

    ESP_LOGI(TAG, "read input reg_2..1");
    req[2] = 0;
    req[3] = 1; // start_addr
    req[4] = 0;
    req[5] = 2; // quantity
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            } else {
                ESP_LOGI(TAG, "[2]:0x%04x [1]:0x%04x",
                    (resp[5] << 8) | resp[6],
                    (resp[3] << 8) | resp[4]);
            }
        }
    }
}

static void process_write_single_coil() {
    uint8_t req[8] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;
    uint16_t value = 0x0000; // 0xff00 - 1, 0x0000 - 0

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_WRITE_SINGLE_COIL;

    ESP_LOGI(TAG, "write coil bit_1");
    req[2] = 0;
    req[3] = 1; // start_addr
    req[4] = value >> 8;
    req[5] = value; // value
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOGI(TAG, "%u", (value == 0xff00) ? 1 : 0);
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            }
        }
    }
}

static void process_write_single_holding() {
    uint8_t req[8] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;
    uint16_t value = 0x3345;

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_WRITE_SINGLE_HOLDING;

    ESP_LOGI(TAG, "write holding reg_1");
    req[2] = 0;
    req[3] = 1; // start_addr
    req[4] = value >> 8;
    req[5] = value; // value
    crc = calc_crc16(req, 6);
    req[6] = crc;
    req[7] = crc >> 8;
    req_len = 8;
    ESP_LOGI(TAG, "0x%04x", value);
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            }
        }
    }
}

static void process_write_multiple_coil() {
    uint8_t req[128] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;
    uint8_t value[2] = {0xbd, 0x03};

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_WRITE_MULTIPLE_COIL;

    ESP_LOGI(TAG, "write coil bit_14..5");
    req[2] = 0;
    req[3] = 5; // start_addr
    req[4] = 0;
    req[5] = 10; // quantity
    req[6] = 2; // value byte count
    req[7] = value[0];
    req[8] = value[1];
    crc = calc_crc16(req, 9);
    req[9] = crc;
    req[10] = crc >> 8;
    req_len = 11;
    ESP_LOGI(TAG, "[14 13][12 11 10 9 8 7 6 5]");
    ESP_LOGI(TAG, "[ %u  %u][ %u  %u  %u %u %u %u %u %u]",
        (value[1] & 0x02) ? 1 : 0,
        value[1] & 0x01,
        (value[0] & 0x80) ? 1 : 0,
        (value[0] & 0x40) ? 1 : 0,
        (value[0] & 0x20) ? 1 : 0,
        (value[0] & 0x10) ? 1 : 0,
        (value[0] & 0x08) ? 1 : 0,
        (value[0] & 0x04) ? 1 : 0,
        (value[0] & 0x02) ? 1 : 0,
        value[0] & 0x01);
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            }
        }
    }
}

static void process_write_multiple_holding() {
    uint8_t req[128] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t crc = 0;
    uint16_t value[3] = {0x5567, 0x7789, 0x9901};

    req[0] = CONFIG_MODBUS_SLAVE_UID;
    req[1] = MODBUS_CMD_WRITE_MULTIPLE_HOLDING;

    ESP_LOGI(TAG, "write holding reg_4..2");
    req[2] = 0;
    req[3] = 2; // start_addr
    req[4] = 0;
    req[5] = 3; // quantity
    req[6] = 6; // value byte count
    req[7] = value[0] >> 8;
    req[8] = value[0];
    req[9] = value[1] >> 8;
    req[10] = value[1];
    req[11] = value[2] >> 8;
    req[12] = value[2];
    crc = calc_crc16(req, 13);
    req[13] = crc;
    req[14] = crc >> 8;
    req_len = 15;
    ESP_LOGI(TAG, "[4]:0x%04x [3]:0x%04x [2]:0x%04x", value[2], value[1], value[0]);
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    uart_write_bytes(CONFIG_MODBUS_UART_PORT, req, req_len);

    resp_len = uart_read_bytes(CONFIG_MODBUS_UART_PORT, resp, sizeof(resp), pdMS_TO_TICKS(CONFIG_MODBUS_RECV_TIMEOUT_MS)); // not return until timeout or rx_buf full
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "recv timeout");
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (check_crc16(resp, resp_len)) {
            ESP_LOGE(TAG, "crc not matched");
        } else {
            if (resp[1] & 0x80) {
                ESP_LOGE(TAG, "err:0x%02x", resp[2]);
            }
        }
    }
}

static void rtu_master_cb() {
    uart_config_t uart_cfg = {
        .baud_rate = CONFIG_MODBUS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(CONFIG_MODBUS_UART_PORT, 1024, 0, 0, NULL, 0);
    uart_param_config(CONFIG_MODBUS_UART_PORT, &uart_cfg);
    uart_set_pin(CONFIG_MODBUS_UART_PORT, CONFIG_MODBUS_UART_PIN_TX, CONFIG_MODBUS_UART_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    process_read_discrete();
    vTaskDelay(pdMS_TO_TICKS(1000));

    process_read_coil();
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_single_coil();
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_multiple_coil();
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_read_coil();
    vTaskDelay(pdMS_TO_TICKS(1000));

    process_read_input();
    vTaskDelay(pdMS_TO_TICKS(1000));

    process_read_holding();
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_single_holding();
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_multiple_holding();
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_read_holding();
    vTaskDelay(pdMS_TO_TICKS(1000));

    vTaskDelete(NULL);
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

    xTaskCreate(rtu_master_cb, "rtu_master", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
