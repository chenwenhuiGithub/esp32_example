#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#define CONFIG_WIFI_SSID                    "SolaxGuest"
#define CONFIG_WIFI_PWD                     "solaxpower"
#define CONFIG_MODBUS_SLAVE_IP              "192.168.108.112"
#define CONFIG_MODBUS_TCP_PORT              502
#define CONFIG_MODBUS_SLAVE_UID             1
#define CONFIG_MODBUS_RECV_TIMEOUT_MS       2000

#define MODBUS_CMD_READ_COIL                0x01
#define MODBUS_CMD_READ_DISCRETE            0x02
#define MODBUS_CMD_READ_HOLDING             0x03
#define MODBUS_CMD_READ_INPUT               0x04
#define MODBUS_CMD_WRITE_SINGLE_COIL        0x05
#define MODBUS_CMD_WRITE_SINGLE_HOLDING     0x06
#define MODBUS_CMD_WRITE_MULTIPLE_COIL      0x0f
#define MODBUS_CMD_WRITE_MULTIPLE_HOLDING   0x10

static const char *TAG = "tcp_master";


static uint16_t gen_trans_id() {
    static uint16_t trans_id = 0;
    return trans_id++;
}

static void process_read_coil(int sock) {
    uint8_t req[12] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;

    ESP_LOGI(TAG, "read coil bit_1");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_COIL;
    req[8] = 0;
    req[9] = 1; // start_addr
    req[10] = 0;
    req[11] = 1; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {
            ESP_LOGI(TAG, "%u", resp[9] & 0x01);                
        }
    }

    ESP_LOGI(TAG, "read coil bit_14..5");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_COIL;
    req[8] = 0;
    req[9] = 5; // start_addr
    req[10] = 0;
    req[11] = 10; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {
            ESP_LOGI(TAG, "[14 13][12 11 10 9 8 7 6 5]");
            ESP_LOGI(TAG, "[ %u  %u][ %u  %u  %u %u %u %u %u %u]",
                (resp[10] & 0x02) ? 1 : 0,
                resp[10] & 0x01,
                (resp[9] & 0x80) ? 1 : 0,
                (resp[9] & 0x40) ? 1 : 0,
                (resp[9] & 0x20) ? 1 : 0,
                (resp[9] & 0x10) ? 1 : 0,
                (resp[9] & 0x08) ? 1 : 0,
                (resp[9] & 0x04) ? 1 : 0,
                (resp[9] & 0x02) ? 1 : 0,
                resp[9] & 0x01);                
        }
    }
}

static void process_read_discrete(int sock) {
    uint8_t req[12] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;

    ESP_LOGI(TAG, "read discrete bit_0");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_DISCRETE;
    req[8] = 0;
    req[9] = 0; // start_addr
    req[10] = 0;
    req[11] = 1; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {
            ESP_LOGI(TAG, "%u", resp[9] & 0x01);                
        }
    }

    ESP_LOGI(TAG, "read discrete bit_9..1");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_DISCRETE;
    req[8] = 0;
    req[9] = 1; // start_addr
    req[10] = 0;
    req[11] = 9; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {
            ESP_LOGI(TAG, "[9][8 7 6 5 4 3 2 1]");
            ESP_LOGI(TAG, "[%u][%u %u %u %u %u %u %u %u]",
                resp[10] & 0x01,
                (resp[9] & 0x80) ? 1 : 0,
                (resp[9] & 0x40) ? 1 : 0,
                (resp[9] & 0x20) ? 1 : 0,
                (resp[9] & 0x10) ? 1 : 0,
                (resp[9] & 0x08) ? 1 : 0,
                (resp[9] & 0x04) ? 1 : 0,
                (resp[9] & 0x02) ? 1 : 0,
                resp[9] & 0x01);           
        }
    }
}

static void process_read_holding(int sock) {
    uint8_t req[12] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;

    ESP_LOGI(TAG, "read holding reg_1");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_HOLDING;
    req[8] = 0;
    req[9] = 1; // start_addr
    req[10] = 0;
    req[11] = 1; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {   
            ESP_LOGI(TAG, "0x%04x", (resp[9] << 8) | resp[10]);           
        }
    }

    ESP_LOGI(TAG, "read holding reg_4..2");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_HOLDING;
    req[8] = 0;
    req[9] = 2; // start_addr
    req[10] = 0;
    req[11] = 3; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {   
            ESP_LOGI(TAG, "[4]:0x%04x [3]:0x%04x [2]:0x%04x",
                (resp[13] << 8) | resp[14],
                (resp[11] << 8) | resp[12],
                (resp[9] << 8) | resp[10]);         
        }
    }
}

static void process_read_input(int sock) {
    uint8_t req[12] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;

    ESP_LOGI(TAG, "read input reg_0");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_INPUT;
    req[8] = 0;
    req[9] = 0; // start_addr
    req[10] = 0;
    req[11] = 1; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {   
            ESP_LOGI(TAG, "0x%04x", (resp[9] << 8) | resp[10]);           
        }
    }

    ESP_LOGI(TAG, "read input reg_2..1");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_READ_INPUT;
    req[8] = 0;
    req[9] = 1; // start_addr
    req[10] = 0;
    req[11] = 2; // quantity
    req_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        } else {   
            ESP_LOGI(TAG, "[2]:0x%04x [1]:0x%04x",
                (resp[11] << 8) | resp[12],
                (resp[9] << 8) | resp[10]);         
        }
    }
}

static void process_write_single_coil(int sock) {
    uint8_t req[12] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;
    uint16_t value = 0x0000; // 0xff00 - 1, 0x0000 - 0

    ESP_LOGI(TAG, "write coil bit_1");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_WRITE_SINGLE_COIL;
    req[8] = 0;
    req[9] = 1; // start_addr
    req[10] = value >> 8;;
    req[11] = value; // value
    req_len = 12;
    ESP_LOGI(TAG, "%u", (value == 0xff00) ? 1 : 0);
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        }
    }
}

static void process_write_single_holding(int sock) {
    uint8_t req[12] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;
    uint16_t value = 0x3345;

    ESP_LOGI(TAG, "write holding reg_1");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 6; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_WRITE_SINGLE_HOLDING;
    req[8] = 0;
    req[9] = 1; // start_addr
    req[10] = value >> 8;;
    req[11] = value; // value
    req_len = 12;
    ESP_LOGI(TAG, "0x%04x", value);
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        }
    }
}

static void process_write_multiple_coil(int sock) {
    uint8_t req[128] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;
    uint8_t value[2] = {0xbd, 0x03};

    ESP_LOGI(TAG, "write coil bit_14..5");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 9; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_WRITE_MULTIPLE_COIL;
    req[8] = 0;
    req[9] = 5; // start_addr
    req[10] = 0;
    req[11] = 10; // quantity
    req[12] = 2; // value byte count
    req[13] = value[0];
    req[14] = value[1];
    req_len = 15;
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
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        }
    }
}

static void process_write_multiple_holding(int sock) {
    uint8_t req[128] = {0};
    uint32_t req_len = 0;
    uint8_t resp[128] = {0};
    int resp_len = 0;
    uint16_t trans_id = 0;
    uint16_t value[3] = {0x5567, 0x7789, 0x9901};

    ESP_LOGI(TAG, "write holding reg_4..2");
    trans_id = gen_trans_id();
    req[0] = trans_id >> 8;
    req[1] = trans_id; // transaction_id
    req[2] = 0;
    req[3] = 0; // protocol_id
    req[4] = 0; 
    req[5] = 13; // len(uid + cmd + data)
    req[6] = CONFIG_MODBUS_SLAVE_UID;
    req[7] = MODBUS_CMD_WRITE_MULTIPLE_HOLDING;
    req[8] = 0;
    req[9] = 2; // start_addr
    req[10] = 0;
    req[11] = 3; // quantity
    req[12] = 6; // value byte count
    req[13] = value[0] >> 8;
    req[14] = value[0];
    req[15] = value[1] >> 8;
    req[16] = value[1];
    req[17] = value[2] >> 8;
    req[18] = value[2];
    req_len = 19;
    ESP_LOGI(TAG, "[4]:0x%04x [3]:0x%04x [2]:0x%04x", value[2], value[1], value[0]);
    ESP_LOG_BUFFER_HEX(TAG, req, req_len);
    send(sock, req, req_len, 0);

    resp_len = recv(sock, resp, sizeof(resp), 0);
    if (resp_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "socket recv timeout");
        } else {
            ESP_LOGE(TAG, "socket recv failed:%d", errno);
        }
    } else if (resp_len == 0) {
        ESP_LOGE(TAG, "socket closed");
        close(sock);
        return;
    } else {
        ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
        if (resp[7] & 0x80) {
            ESP_LOGE(TAG, "err:0x%02x", resp[8]);
        }
    }
}

static void tcp_master_cb(void *pvParameters) {
    int sock = 0;
    int err = 0;
    struct sockaddr_in server_addr = {0};
    struct timeval tv = {.tv_sec = CONFIG_MODBUS_RECV_TIMEOUT_MS, .tv_usec = 0};

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        goto exit;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(CONFIG_MODBUS_SLAVE_IP);
    server_addr.sin_port = htons(CONFIG_MODBUS_TCP_PORT);
    ESP_LOGI(TAG, "client start connect %s:%u", CONFIG_MODBUS_SLAVE_IP, CONFIG_MODBUS_TCP_PORT);
    err = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket connect failed:%d", errno);
        goto exit;
    }
    ESP_LOGI(TAG, "socket connect success");

    err = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (err != 0) {
        ESP_LOGE(TAG, "socket setsockopt failed:%d", errno);
        close(sock);
        goto exit;
    }

    process_read_discrete(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));

    process_read_coil(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_single_coil(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_multiple_coil(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_read_coil(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));

    process_read_input(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));

    process_read_holding(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_single_holding(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_write_multiple_holding(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    process_read_holding(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));

exit:
    vTaskDelete(NULL);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    wifi_event_sta_connected_t* evt_sta_conn = NULL;
    wifi_event_sta_disconnected_t* evt_sta_dis = NULL;
    ip_event_got_ip_t* evt_got_ip = NULL;

    if (WIFI_EVENT == event_base) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            ESP_LOGI(TAG, "start connect, ssid:%s", CONFIG_WIFI_SSID);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            evt_sta_conn = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED, channel:%u, authmode:%u", evt_sta_conn->channel, evt_sta_conn->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            evt_sta_dis = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "WIFI_EVENT_STA_DISCONNECTED, reason:%u", evt_sta_dis->reason);
            break;
        default:
            ESP_LOGW(TAG, "unknown WIFI_EVENT:%ld", event_id);
            break;
        }
    }

    if (IP_EVENT == event_base) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            evt_got_ip = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&evt_got_ip->ip_info.ip), IP2STR(&evt_got_ip->ip_info.netmask), IP2STR(&evt_got_ip->ip_info.gw));
            xTaskCreate(tcp_master_cb, "tcp_master", 4096, NULL, 5, NULL);
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGE(TAG, "IP_EVENT_STA_LOST_IP");
            break;
        default:
            ESP_LOGW(TAG, "unknown IP_EVENT:%ld", event_id);
            break;   
        }
    }
}

void app_main(void) {
    esp_err_t err = ESP_OK;
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PWD,
        },
    };

    err = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == err || ESP_ERR_NVS_NEW_VERSION_FOUND == err) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "nvs_flash_init error:%d", err);
        return;
    }

    esp_event_loop_create_default();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);

    esp_netif_init();
    esp_netif_create_default_wifi_sta();

    esp_wifi_init(&init_cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
