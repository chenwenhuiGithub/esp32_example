#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_modbus_common.h"
#include "esp_modbus_slave.h"


#define EXAMPLE_WIFI_SSID           "SolaxGuest"
#define EXAMPLE_WIFI_PWD            "solaxpower"
#define EXAMPLE_MODBUS_TCP_PORT     502
#define EXAMPLE_MODBUS_SLAVE_UID    1

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

static const char *TAG = "modbus_tcp_slave";
static esp_netif_t *sta_netif = NULL;
static void* hd_tcp_slave = NULL;
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

static void tcp_slave_task(void *pvParameters) {
    esp_err_t err = ESP_OK;
    mb_communication_info_t comm_info = {0};
    mb_register_area_descriptor_t reg_area = {0};
    mb_param_info_t para_info = {0};

    init_slave_data();

    err = mbc_slave_init_tcp(&hd_tcp_slave);
    if ((ESP_OK != err) || (hd_tcp_slave == NULL)) {
        ESP_LOGE(TAG, "mbc_slave_init_tcp error:%d", err);
        return;
    }

    comm_info.ip_mode = MB_MODE_TCP;
    comm_info.ip_addr_type = MB_IPV4;
    comm_info.ip_addr = NULL;
    comm_info.ip_port = EXAMPLE_MODBUS_TCP_PORT;
    comm_info.ip_netif_ptr = sta_netif;
    comm_info.slave_uid = EXAMPLE_MODBUS_SLAVE_UID;
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

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    wifi_event_sta_connected_t* conn_event = NULL;
    wifi_event_sta_disconnected_t* dis_event = NULL;
    ip_event_got_ip_t* got_event = NULL;

    if (WIFI_EVENT == event_base) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "start connect to %s", EXAMPLE_WIFI_SSID);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            conn_event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "connected, channel:%u, authmode:%u", conn_event->channel, conn_event->authmode);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            dis_event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE(TAG, "disconnected, reason:%u", dis_event->reason);
            break;
        default:
            ESP_LOGW(TAG, "unknown WIFI_EVENT:%ld", event_id);
            break;
        }
    }
    
    if (IP_EVENT == event_base) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            got_event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "get ip, ip:" IPSTR " netmask:" IPSTR " gw:" IPSTR,
                IP2STR(&got_event->ip_info.ip), IP2STR(&got_event->ip_info.netmask), IP2STR(&got_event->ip_info.gw));
            xTaskCreate(tcp_slave_task, "tcp_slave_task", 4096, NULL, 5, NULL);
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGE(TAG, "lost ip");
            break;
        default:
            ESP_LOGW(TAG, "unknown IP_EVENT:%ld", event_id);
            break;   
        }
    }
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

    esp_event_loop_create_default();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);

    esp_netif_init();
    sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);

    wifi_config_t sta_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PWD,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_start();

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
