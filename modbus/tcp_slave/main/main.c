#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_modbus_common.h"
#include "esp_modbus_slave.h"


#define CONFIG_WIFI_SSID                "SolaxGuest"
#define CONFIG_WIFI_PWD                 "solaxpower"
#define CONFIG_MODBUS_TCP_PORT          502
#define CONFIG_MODBUS_SLAVE_UID         1

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


static const char *TAG = "tcp_slave";
static esp_netif_t *sta_netif = NULL;
static void* hd_tcp_slave = NULL;
static discrete_input_t discrete_input = {0};
static coil_t coil = {0};
static input_reg_t input_reg = {0};
static holding_reg_t holding_reg = {0};


static void init_slave_data(void) {
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

static void tcp_slave_cb(void *pvParameters) {
    esp_err_t err = ESP_OK;
    mb_communication_info_t comm_info = {0};
    mb_register_area_descriptor_t reg_area = {0};
    mb_param_info_t para_info = {0};

    init_slave_data();

    comm_info.tcp_opts.mode = MB_TCP;
    comm_info.tcp_opts.port = CONFIG_MODBUS_TCP_PORT;
    comm_info.tcp_opts.uid = CONFIG_MODBUS_SLAVE_UID;
    comm_info.tcp_opts.addr_type = MB_IPV4;
    comm_info.tcp_opts.ip_addr_table = NULL; // bind to any address
    comm_info.tcp_opts.ip_netif_ptr = sta_netif;
    err = mbc_slave_create_tcp(&comm_info, &hd_tcp_slave);
    if ((ESP_OK != err) || (NULL == hd_tcp_slave)) {
        ESP_LOGE(TAG, "mbc_slave_create_tcp error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_DISCRETE;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&discrete_input;
    reg_area.size = sizeof(discrete_input);
    err = mbc_slave_set_descriptor(hd_tcp_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_DISCRETE error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&coil;
    reg_area.size = sizeof(coil);
    err = mbc_slave_set_descriptor(hd_tcp_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_COIL error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&input_reg;
    reg_area.size = sizeof(input_reg);
    err = mbc_slave_set_descriptor(hd_tcp_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_INPUT error:%d", err);
        return;
    }

    reg_area.type = MB_PARAM_HOLDING;
    reg_area.start_offset = 0;
    reg_area.address = (void*)&holding_reg;
    reg_area.size = sizeof(holding_reg);
    err = mbc_slave_set_descriptor(hd_tcp_slave, reg_area);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor MB_PARAM_HOLDING error:%d", err);
        return;
    }

    err = mbc_slave_start(hd_tcp_slave);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "mbc_slave_start error:%d", err);
        return;
    }
    ESP_LOGI(TAG, "mbc_slave_start success");

    while (1) {
        mbc_slave_check_event(hd_tcp_slave,
            MB_EVENT_DISCRETE_RD | MB_EVENT_COILS_RD | MB_EVENT_COILS_WR | MB_EVENT_INPUT_REG_RD | MB_EVENT_HOLDING_REG_RD | MB_EVENT_HOLDING_REG_WR);
        err = mbc_slave_get_param_info(hd_tcp_slave, &para_info, 500);
        if (ESP_OK == err) {
            ESP_LOGI(TAG, "timestamp:%lu offset:%u type:%u(%s) address:%p size:%u",
                para_info.time_stamp, para_info.mb_offset, para_info.type, get_event_type_string(para_info.type), para_info.address, para_info.size);
        }
    }
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
    sta_netif = esp_netif_create_default_wifi_sta();
    
    esp_wifi_init(&init_cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
