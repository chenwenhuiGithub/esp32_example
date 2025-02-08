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


#define CONFIG_WIFI_SSID                        "SolaxGuest"
#define CONFIG_WIFI_PWD                         "solaxpower"
#define CONFIG_MODBUS_TCP_PORT                  502
#define CONFIG_MODBUS_SLAVE_UID                 1
#define CONFIG_MODBUS_CLIENT_SIZE               3
#define CONFIG_MODBUS_DISCRETE_SIZE             10
#define CONFIG_MODBUS_COIL_SIZE                 20
#define CONFIG_MODBUS_INPUT_SIZE                3
#define CONFIG_MODBUS_HOLDING_SIZE              5

#define MODBUS_CMD_READ_COIL                    0x01
#define MODBUS_CMD_READ_DISCRETE                0x02
#define MODBUS_CMD_READ_HOLDING                 0x03
#define MODBUS_CMD_READ_INPUT                   0x04
#define MODBUS_CMD_WRITE_SINGLE_COIL            0x05
#define MODBUS_CMD_WRITE_SINGLE_HOLDING         0x06
#define MODBUS_CMD_WRITE_MULTIPLE_COIL          0x0f
#define MODBUS_CMD_WRITE_MULTIPLE_HOLDING       0x10

#define MODBUS_ERR_ILLEGAL_FUNC                 0x01
#define MODBUS_ERR_ILLEGAL_DATA_ADDR            0x02
#define MODBUS_ERR_ILLEGAL_DATA_VALUE           0x03
#define MODBUS_ERR_SLAVE_FAILURE                0x04


static const char *TAG = "tcp_slave";
static uint8_t discrete[(CONFIG_MODBUS_DISCRETE_SIZE / 8) + (CONFIG_MODBUS_DISCRETE_SIZE % 8 ? 1 : 0)] = {0};
static uint8_t coil[(CONFIG_MODBUS_COIL_SIZE / 8) + (CONFIG_MODBUS_COIL_SIZE % 8 ? 1 : 0)] = {0};
static uint16_t input[CONFIG_MODBUS_INPUT_SIZE] = {0};
static uint16_t holding[CONFIG_MODBUS_HOLDING_SIZE] = {0};

static void init_slave_data(void) {
    discrete[0] |= 0x11;
    discrete[1] |= 0x02; // bit 0,4,9

    coil[0] |= 0x42;
    coil[1] |= 0x08;
    coil[2] |= 0x01;     // bit 1,6,11,16

    input[0] = 0x1234;
    input[1] = 0x5678;
    input[2] = 0xabcd;

    holding[0] = 0x1122;
    holding[1] = 0x3344;
    holding[2] = 0x5566;
    holding[3] = 0x7788;
    holding[4] = 0x9900;
}

static void response_err(int sock, uint8_t err, uint8_t *data, uint32_t len) {
    uint8_t resp[9] = {0};
    uint32_t resp_len = 0;

    resp[0] = data[0];
    resp[1] = data[1]; // transaction_id
    resp[2] = data[2];
    resp[3] = data[3]; // protocol_id
    resp[4] = 0;
    resp[5] = 3; // len(uid + cmd + data)
    resp[6] = data[6]; // uid
    resp[7] = data[7] | 0x80; // cmd
    resp[8] = err; // data: err code
    resp_len = 9;
    ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
    send(sock, resp, resp_len, 0);
}

static void process_read_coil(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, quantity = 0;
    uint8_t resp[128] = {0};
    uint32_t resp_len = 0;
    uint16_t i = 0, src_byte_index = 0, src_bit_index = 0, des_byte_index = 0, des_bit_index = 0;
    uint8_t value_byte_cnt = 0;

    start_addr = (data[8] << 8) | data[9];
    quantity = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "read coil, start_addr:0x%04x quantity:%u", start_addr, quantity);

    if ((start_addr >= CONFIG_MODBUS_COIL_SIZE) || (quantity > CONFIG_MODBUS_COIL_SIZE)) {
        ESP_LOGE(TAG, "invalid para, coil size:%u", CONFIG_MODBUS_COIL_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    for (i = 0; i < quantity; i++) {
        src_byte_index = (start_addr + i) / 8;
        src_bit_index = (start_addr + i) % 8;
        des_byte_index = i / 8;
        des_bit_index = i % 8;
        if (coil[src_byte_index] & (1 << src_bit_index)) {
            resp[des_byte_index + 9] |= (1 << des_bit_index); // data: coil value
        }
    }

    value_byte_cnt = (quantity / 8) + (quantity % 8 ? 1 : 0);
    resp[0] = data[0];
    resp[1] = data[1]; // transaction_id
    resp[2] = data[2];
    resp[3] = data[3]; // protocol_id
    resp[4] = (value_byte_cnt + 3) >> 8;
    resp[5] = value_byte_cnt + 3; // len(uid + cmd + data)
    resp[6] = data[6]; // uid
    resp[7] = data[7]; // cmd
    resp[8] = value_byte_cnt; // data: coil byte cnt
    resp_len = value_byte_cnt + 9;
    ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
    send(sock, resp, resp_len, 0);
}

static void process_read_discrete(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, quantity = 0;
    uint8_t resp[128] = {0};
    uint32_t resp_len = 0;
    uint16_t i = 0, src_byte_index = 0, src_bit_index = 0, des_byte_index = 0, des_bit_index = 0;
    uint8_t value_byte_cnt = 0;

    start_addr = (data[8] << 8) | data[9];
    quantity = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "read discrete, start_addr:0x%04x quantity:%u", start_addr, quantity);

    if ((start_addr >= CONFIG_MODBUS_DISCRETE_SIZE) || (quantity > CONFIG_MODBUS_DISCRETE_SIZE)) {
        ESP_LOGE(TAG, "invalid para, discrete size:%u", CONFIG_MODBUS_DISCRETE_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    for (i = 0; i < quantity; i++) {
        src_byte_index = (start_addr + i) / 8;
        src_bit_index = (start_addr + i) % 8;
        des_byte_index = i / 8;
        des_bit_index = i % 8;
        if (discrete[src_byte_index] & (1 << src_bit_index)) {
            resp[des_byte_index + 9] |= (1 << des_bit_index); // data: discrete value
        }
    }

    value_byte_cnt = (quantity / 8) + (quantity % 8 ? 1 : 0);
    resp[0] = data[0];
    resp[1] = data[1]; // transaction_id
    resp[2] = data[2];
    resp[3] = data[3]; // protocol_id
    resp[4] = (value_byte_cnt + 3) >> 8;
    resp[5] = value_byte_cnt + 3; // len(uid + cmd + data)
    resp[6] = data[6]; // uid
    resp[7] = data[7]; // cmd
    resp[8] = value_byte_cnt; // data: discrete byte cnt
    resp_len = value_byte_cnt + 9;
    ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
    send(sock, resp, resp_len, 0);
}

static void process_read_holding(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, quantity = 0;
    uint8_t resp[128] = {0};
    uint32_t resp_len = 0;
    uint16_t i = 0;
    uint8_t value_byte_cnt = 0;

    start_addr = (data[8] << 8) | data[9];
    quantity = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "read holding, start_addr:0x%04x quantity:%u", start_addr, quantity);

    if ((start_addr >= CONFIG_MODBUS_HOLDING_SIZE) || (quantity > CONFIG_MODBUS_HOLDING_SIZE)) {
        ESP_LOGE(TAG, "invalid para, holding size:%u", CONFIG_MODBUS_HOLDING_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    for (i = 0; i < quantity; i++) {
        resp[i * 2 + 9] = holding[start_addr + i] >> 8;
        resp[i * 2 + 10] = holding[start_addr + i]; // data: holding value
    }

    value_byte_cnt = quantity * 2;
    resp[0] = data[0];
    resp[1] = data[1]; // transaction_id
    resp[2] = data[2];
    resp[3] = data[3]; // protocol_id
    resp[4] = (value_byte_cnt + 3) >> 8;
    resp[5] = value_byte_cnt + 3; // len(uid + cmd + data)
    resp[6] = data[6]; // uid
    resp[7] = data[7]; // cmd
    resp[8] = value_byte_cnt; // data: holding byte cnt
    resp_len = value_byte_cnt + 9;
    ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
    send(sock, resp, resp_len, 0);
}

static void process_read_input(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, quantity = 0;
    uint8_t resp[128] = {0};
    uint32_t resp_len = 0;
    uint16_t i = 0;
    uint8_t value_byte_cnt = 0;

    start_addr = (data[8] << 8) | data[9];
    quantity = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "read input, start_addr:0x%04x quantity:%u", start_addr, quantity);

    if ((start_addr >= CONFIG_MODBUS_INPUT_SIZE) || (quantity > CONFIG_MODBUS_INPUT_SIZE)) {
        ESP_LOGE(TAG, "invalid para, input size:%u", CONFIG_MODBUS_INPUT_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    for (i = 0; i < quantity; i++) {
        resp[i * 2 + 9] = input[start_addr + i] >> 8;
        resp[i * 2 + 10] = input[start_addr + i]; // data: input value
    }

    value_byte_cnt = quantity * 2;
    resp[0] = data[0];
    resp[1] = data[1]; // transaction_id
    resp[2] = data[2];
    resp[3] = data[3]; // protocol_id
    resp[4] = (value_byte_cnt + 3) >> 8;
    resp[5] = value_byte_cnt + 3; // len(uid + cmd + data)
    resp[6] = data[6]; // uid
    resp[7] = data[7]; // cmd
    resp[8] = value_byte_cnt; // data: input byte cnt
    resp_len = value_byte_cnt + 9;
    ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
    send(sock, resp, resp_len, 0);
}

static void process_write_single_coil(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, value = 0;
    uint16_t byte_index = 0, bit_index = 0;

    start_addr = (data[8] << 8) | data[9];
    value = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "write single coil, start_addr:0x%04x value:0x%04x", start_addr, value);

    if (start_addr >= CONFIG_MODBUS_COIL_SIZE) {
        ESP_LOGE(TAG, "invalid para, coil size:%u", CONFIG_MODBUS_COIL_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    byte_index = start_addr / 8;
    bit_index = start_addr % 8;
    if (0xff00 == value) {
        coil[byte_index] |= (1 << bit_index);
    } else if (0x0000 == value) {
        coil[byte_index] &= ~(1 << bit_index);
    } else {
        ESP_LOGE(TAG, "unknown value");
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_VALUE, data, len);
        return;
    }
    ESP_LOG_BUFFER_HEX(TAG, data, len);
    send(sock, data, len, 0);
}

static void process_write_single_holding(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, value = 0;

    start_addr = (data[8] << 8) | data[9];
    value = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "write single holding, start_addr:0x%04x value:0x%04x", start_addr, value);

    if (start_addr >= CONFIG_MODBUS_HOLDING_SIZE) {
        ESP_LOGE(TAG, "invalid para, holding size:%u", CONFIG_MODBUS_HOLDING_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    holding[start_addr] = value;

    ESP_LOG_BUFFER_HEX(TAG, data, len);
    send(sock, data, len, 0);
}

static void process_write_multiple_coil(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, quantity = 0;
    uint8_t resp[12] = {0};
    uint32_t resp_len = 0;
    uint16_t i = 0, src_byte_index = 0, src_bit_index = 0, des_byte_index = 0, des_bit_index = 0;

    start_addr = (data[8] << 8) | data[9];
    quantity = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "write multiple coil, start_addr:0x%04x quantity:%u", start_addr, quantity);

    if ((start_addr >= CONFIG_MODBUS_COIL_SIZE) || (quantity > CONFIG_MODBUS_COIL_SIZE)) {
        ESP_LOGE(TAG, "invalid para, coil size:%u", CONFIG_MODBUS_COIL_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    for (i = 0; i < quantity; i++) {
        src_byte_index = i / 8;
        src_bit_index = i % 8;
        des_byte_index = (start_addr + i) / 8;
        des_bit_index = (start_addr + i) % 8;
        if (data[src_byte_index + 13] & (1 << src_bit_index)) {
            coil[des_byte_index] |= (1 << des_bit_index);
        } else {
            coil[des_byte_index] &= ~(1 << des_bit_index);
        }
    }

    resp[0] = data[0];
    resp[1] = data[1]; // transaction_id
    resp[2] = data[2];
    resp[3] = data[3]; // protocol_id
    resp[4] = 0;
    resp[5] = 6; // len(uid + cmd + data)
    resp[6] = data[6]; // uid
    resp[7] = data[7]; // cmd
    resp[8] = data[8];
    resp[9] = data[9]; // start_addr
    resp[10] = data[10];
    resp[11] = data[11]; // quantity
    resp_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
    send(sock, resp, resp_len, 0);
}

static void process_write_multiple_holding(int sock, uint8_t *data, uint32_t len) {
    uint16_t start_addr = 0, quantity = 0;
    uint8_t resp[12] = {0};
    uint32_t resp_len = 0;
    uint16_t i = 0;

    start_addr = (data[8] << 8) | data[9];
    quantity = (data[10] << 8) | data[11];
    ESP_LOGI(TAG, "write multiple holding, start_addr:0x%04x quantity:%u", start_addr, quantity);

    if ((start_addr >= CONFIG_MODBUS_HOLDING_SIZE) || (quantity > CONFIG_MODBUS_HOLDING_SIZE)) {
        ESP_LOGE(TAG, "invalid para, holding size:%u", CONFIG_MODBUS_HOLDING_SIZE);
        response_err(sock, MODBUS_ERR_ILLEGAL_DATA_ADDR, data, len);
        return;
    }

    for (i = 0; i < quantity; i++) {
        holding[start_addr + i] = (uint16_t)(data[i * 2 + 13] << 8) | data[i * 2 + 14];
    }

    resp[0] = data[0];
    resp[1] = data[1]; // transaction_id
    resp[2] = data[2];
    resp[3] = data[3]; // protocol_id
    resp[4] = 0;
    resp[5] = 6; // len(uid + cmd + data)
    resp[6] = data[6]; // uid
    resp[7] = data[7]; // cmd
    resp[8] = data[8];
    resp[9] = data[9]; // start_addr
    resp[10] = data[10];
    resp[11] = data[11]; // quantity
    resp_len = 12;
    ESP_LOG_BUFFER_HEX(TAG, resp, resp_len);
    send(sock, resp, resp_len, 0);
}

// [0..1]:transId
// [2..3]:protoId
// [4..5]:length = uid(1B) + cmd(1B) + data(NB)
// [6]:uid
// [7]:cmd
// [8..]:data
static void process_cmd(int sock, uint8_t *data, uint32_t len) {
    uint8_t uid = 0, cmd = 0;

    uid = data[6];
    cmd = data[7];

    if (CONFIG_MODBUS_SLAVE_UID != uid) {
        ESP_LOGE(TAG, "uid not matched, slave:0x%02x master:0x%02x", CONFIG_MODBUS_SLAVE_UID, uid);
        response_err(sock, MODBUS_ERR_SLAVE_FAILURE, data, len);
        return;
    }

    switch (cmd) {
    case MODBUS_CMD_READ_COIL:
        process_read_coil(sock, data, len);
        break;
    case MODBUS_CMD_READ_DISCRETE:
        process_read_discrete(sock, data, len);
        break;
    case MODBUS_CMD_READ_HOLDING:
        process_read_holding(sock, data, len);
        break;
    case MODBUS_CMD_READ_INPUT:
        process_read_input(sock, data, len);
        break;
    case MODBUS_CMD_WRITE_SINGLE_COIL:
        process_write_single_coil(sock, data, len);
        break;
    case MODBUS_CMD_WRITE_SINGLE_HOLDING:
        process_write_single_holding(sock, data, len);
        break;
    case MODBUS_CMD_WRITE_MULTIPLE_COIL:
        process_write_multiple_coil(sock, data, len);
        break;
    case MODBUS_CMD_WRITE_MULTIPLE_HOLDING:
        process_write_multiple_holding(sock, data, len);
        break;
    default:
        ESP_LOGW(TAG, "unknown cmd:0x%02x", cmd);
        response_err(sock, MODBUS_ERR_ILLEGAL_FUNC, data, len);
        break;
    }
}

static void tcp_slave_cb(void *pvParameters) {
    int listen_sock = 0;
    int client_sock = 0;
    int err = 0;
    int rx_len = 0;
    uint8_t rx_data[128] = {0};
    struct sockaddr_in local_addr = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    uint32_t i = 0;
    int flags = 0;
    int client_socks[CONFIG_MODBUS_CLIENT_SIZE] = {0};
    int max_fd = 0;
    fd_set readfds;
    struct timeval tv = {.tv_sec = 3, .tv_usec = 0};
    int select_cnt = 0;

    init_slave_data();

    for (i = 0; i < CONFIG_MODBUS_CLIENT_SIZE; i++) {
        client_socks[i] = -1;
    }

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        goto exit;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(CONFIG_MODBUS_TCP_PORT);
    err = bind(listen_sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (0 != err) {
        ESP_LOGE(TAG, "socket bind failed:%d", errno);
        goto exit;
    }

    flags = fcntl(listen_sock, F_GETFL);
    err = fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK);
    if (err < 0) {
        ESP_LOGE(TAG, "socket fcntl failed:%d", errno);
        goto exit;
    }

    listen(listen_sock, 1);
    ESP_LOGI(TAG, "socket listen:%u", CONFIG_MODBUS_TCP_PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(listen_sock, &readfds);
        max_fd = listen_sock;
        for (i = 0; i < CONFIG_MODBUS_CLIENT_SIZE; i++) {
            if (-1 != client_socks[i]) {
                FD_SET(client_socks[i], &readfds);
                if (client_socks[i] > max_fd) {
                    max_fd = client_socks[i];
                }
            }
        }

        select_cnt = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (select_cnt < 0) {
            ESP_LOGE(TAG, "socket select failed:%d", errno);
            goto exit; 
        } else if (0 == select_cnt) {
            // ESP_LOGW(TAG, "socket select timeout");
            continue;
        } else {
            if (FD_ISSET(listen_sock, &readfds)) {
                client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
                if (client_sock < 0) {
                    ESP_LOGE(TAG, "socket accept failed:%d", errno);
                } else {
                    ESP_LOGI(TAG, "client connected, %s:%u", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
                    for (i = 0; i < CONFIG_MODBUS_CLIENT_SIZE; i++) {
                        if (client_socks[i] == -1) {
                            client_socks[i] = client_sock;
                            break;
                        }
                    }

                    if (CONFIG_MODBUS_CLIENT_SIZE == i) {
                        ESP_LOGW(TAG, "max clients reached, close new connection");
                        close(client_sock);
                    } else {
                        flags = fcntl(client_sock, F_GETFL);
                        err = fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);
                        if (err < 0) {
                            ESP_LOGE(TAG, "socket fcntl failed:%d", errno);
                            close(client_sock);
                            client_socks[i] = -1;
                        }
                    }
                }
            }

            for (i = 0; i < CONFIG_MODBUS_CLIENT_SIZE; i++) {
                if ((-1 != client_socks[i]) && (FD_ISSET(client_socks[i], &readfds))) {
                    rx_len = recv(client_socks[i], rx_data, sizeof(rx_data), 0);
                    if (rx_len < 0) {
                        ESP_LOGE(TAG, "socket recv failed:%d", errno);
                        close(client_socks[i]);
                        client_socks[i] = -1;
                    } else if (rx_len == 0) {
                        ESP_LOGW(TAG, "socket closed");
                        close(client_socks[i]);
                        client_socks[i] = -1;
                    } else {
                        ESP_LOG_BUFFER_HEX(TAG, rx_data, rx_len);
                        process_cmd(client_socks[i], rx_data, rx_len);
                    }                      
                }                    
            }
        }
    }

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
            xTaskCreate(tcp_slave_cb, "tcp_slave", 4096, NULL, 5, NULL);
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
