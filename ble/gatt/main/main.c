#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"


#define TAG                             "ble_gatt"
#define EXAMPLE_APP_ID                  0x0055
#define EXAMPLE_SVC_INST_ID_BATTERY     0
#define EXAMPLE_SVC_INST_ID_TEST        1

enum
{
    IDX_SVC_BATTERY,
    IDX_CHAR_DEC_BATTERY_LEVEL,
    IDX_CHAR_VAL_BATTERY_LEVEL,
    IDX_NUM_BATTERY,
};

enum
{
    IDX_SVC_TEST,
    IDX_CHAR_DEC_RX,
    IDX_CHAR_VAL_RX,
    IDX_CHAR_DEC_TX,
    IDX_CHAR_VAL_TX,
    IDX_CHAR_CCC_TX,
    IDX_NUM_TEST,
};

static uint8_t adv_data[] = {
    0x02, 0x01, 0x06,           // flags 
    0x03, 0x03, 0xFF, 0x00,     // service uuid
};

static uint8_t scan_rsp_data[] = {
    0x0f, 0x09, 'E', 'S', 'P', '_', 'G', 'A', 'T', 'T', 'S', '_', 'D','E', 'M', 'O'     // device name
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min         = 0x20,
    .adv_int_max         = 0x40,
    .adv_type            = ADV_TYPE_IND,
    .own_addr_type       = BLE_ADDR_TYPE_PUBLIC,
    .channel_map         = ADV_CHNL_ALL,
    .adv_filter_policy   = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static const uint16_t uuid_svc_battery              = 0x180F;
static const uint16_t uuid_svc_test                 = 0x18FF;
static const uint16_t uuid_object_battery_level     = 0x2A19;
static const uint16_t uuid_object_rx                = 0x2AFE;
static const uint16_t uuid_object_tx                = 0x2AFF;
static const uint16_t uuid_pri_svc                  = 0x2800;
static const uint16_t uuid_char_dec                 = 0x2803;
static const uint16_t uuid_char_ccc                 = 0x2902;
static const uint8_t char_prop_battery_level        = ESP_GATT_CHAR_PROP_BIT_READ;      // 0x02
static const uint8_t char_prop_rx                   = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;  // 0x04
static const uint8_t char_prop_tx                   = ESP_GATT_CHAR_PROP_BIT_NOTIFY;    // 0x10
static uint8_t char_val_battery_level               = 0x14; // 20%
static uint8_t char_val_rx[32]                      = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
                                                        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,};
static uint8_t char_val_tx[32]                      = { 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 
                                                        0x0A, 0x1A, 0x2A, 0x3A, 0x4A, 0x5A, 0x6A, 0x7A, 0x8A, 0x9A, 0xAA, 0xBA, 0xCA, 0xDA, 0xEA, 0xFA};
static uint16_t char_ccc_tx                         = 0x0001;    // 0x01 - notify, 0x02 - indication
static uint16_t handles_battery[IDX_NUM_BATTERY]    = { 0 };
static uint16_t handles_test[IDX_NUM_TEST]          = { 0 };

/*
att_handle(2B)  att_type(UUID, 2B/16B)                          att_value(0-512B)                                           att_permission
// BATTERY service
0x0028          0x2800(GATT_DECLARATION_PRIMARY_SERVICE)        0x180F(GATT_SERVICE_BATTERY)                                GATT_PERMISSION_READ
0x0029          0x2803(GATT_DECLARATION_CHARACTERISTIC)         0x32(GATT_CHARACTERISTIC_PROPERITY_READ)                    GATT_PERMISSION_READ
                                                                0x0103(handle)
                                                                0x2A19(GATT_OBJECT_TYPE_BATTERY_LEVEL)
0x002A          0x2A19(GATT_OBJECT_TYPE_BATTERY_LEVEL)          [1B]0x14(20%)                                               GATT_PERMISSION_READ

// TEST service
0x002B          0x2800(GATT_DECLARATION_PRIMARY_SERVICE)        0x18FF(GATT_SERVICE_TEST)                                   GATT_PERMISSION_READ
0x002C          0x2803(GATT_DECLARATION_CHARACTERISTIC)         0x04(GATT_CHARACTERISTIC_PROPERITY_WRITE_NORESP)            GATT_PERMISSION_READ
                                                                0x1003(handle)
                                                                0x2AFE(GATT_OBJECT_TYPE_TEST_RX)
0x002D          0x2AFE(GATT_OBJECT_TYPE_TEST_RX)                [32B]                                                      GATT_PERMISSION_WRITE
0x002E          0x2803(GATT_DECLARATION_CHARACTERISTIC)         0x10(GATT_CHARACTERISTIC_PROPERITY_NOTIFY)                  GATT_PERMISSION_READ
                                                                0x1012(handle)
                                                                0x2AFF(GATT_OBJECT_TYPE_TEST_TX)
0x002F          0x2AFF(GATT_OBJECT_TYPE_TEST_TX)                [32B]                                                      GATT_PERMISSION_READ
0x0030          0x2902(GATT_CLIENT_CHARACTER_CONFIG)            [2B]0x0000                                                  GATT_PERMISSION_READ|WRITE
*/

static const esp_gatts_attr_db_t gatt_db_battery[IDX_NUM_BATTERY] =
{
    // Service Declaration
    [IDX_SVC_BATTERY]           =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_pri_svc, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&uuid_svc_battery}},

    // Characteristic Declaration
    [IDX_CHAR_DEC_BATTERY_LEVEL]    =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_char_dec, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_prop_battery_level}},

    // Characteristic Value
    [IDX_CHAR_VAL_BATTERY_LEVEL]    =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_object_battery_level, ESP_GATT_PERM_READ, sizeof(char_val_battery_level), sizeof(char_val_battery_level), (uint8_t *)&char_val_battery_level}}
};

static const esp_gatts_attr_db_t gatt_db_test[IDX_NUM_TEST] =
{
    // Service Declaration
    [IDX_SVC_TEST]              =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_pri_svc, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&uuid_svc_test}},

    // Characteristic Declaration
    [IDX_CHAR_DEC_RX]           =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_char_dec, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_prop_rx}},

    // Characteristic Value
    [IDX_CHAR_VAL_RX]           =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_object_rx, ESP_GATT_PERM_WRITE, sizeof(char_val_rx), 0, char_val_rx}},

    // Characteristic Declaration
    [IDX_CHAR_DEC_TX]           =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_char_dec, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_prop_tx}},

    // Characteristic Value
    [IDX_CHAR_VAL_TX]           =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_object_tx, ESP_GATT_PERM_READ, sizeof(char_val_tx), 0, char_val_tx}},

    // Client Characteristic Configuration Descriptor
    [IDX_CHAR_CCC_TX]           =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_char_ccc, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&char_ccc_tx}}
};

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "GATTS_REG_EVT, status:0x%02X app_id:0x%04X", param->reg.status, param->reg.app_id);
            esp_err_t err = ESP_OK;
            err = esp_ble_gap_config_adv_data_raw(adv_data, sizeof(adv_data));
            if (err) {
                ESP_LOGE(TAG, "config adv data failed:%d", err);
            }
            err = esp_ble_gap_config_scan_rsp_data_raw(scan_rsp_data, sizeof(scan_rsp_data));
            if (err) {
                ESP_LOGE(TAG, "config scan rsp data failed:%d", err);
            }
            err = esp_ble_gatts_create_attr_tab(gatt_db_battery, gatts_if, IDX_NUM_BATTERY, EXAMPLE_SVC_INST_ID_BATTERY); // trigger ESP_GATTS_CREAT_ATTR_TAB_EVT
            if (err) {
                ESP_LOGE(TAG, "create att table battery failed:%d", err);
            }
            err = esp_ble_gatts_create_attr_tab(gatt_db_test, gatts_if, IDX_NUM_TEST, EXAMPLE_SVC_INST_ID_TEST);
            if (err) {
                ESP_LOGE(TAG, "create att table test failed:%d", err);
            }
            err = esp_ble_gap_start_advertising(&adv_params);
            if (err) {
                ESP_LOGE(TAG, "start advertising failed:%d", err);
            }
       	    break;
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            ESP_LOGI(TAG, "GATTS_CREAT_ATTR_TAB_EVT, status:0x%02X svc_uuid:0x%04X svc_inst_id:%u num_handle:%u handles:",
                param->add_attr_tab.status, param->add_attr_tab.svc_uuid.uuid.uuid16, param->add_attr_tab.svc_inst_id, param->add_attr_tab.num_handle);
            if (EXAMPLE_SVC_INST_ID_BATTERY == param->add_attr_tab.svc_inst_id) {
                ESP_LOGI(TAG, "0x%04X 0x%04X 0x%04X", param->add_attr_tab.handles[0], param->add_attr_tab.handles[1], param->add_attr_tab.handles[2]);
                memcpy(handles_battery, param->add_attr_tab.handles, sizeof(uint16_t) * param->add_attr_tab.num_handle);
                esp_ble_gatts_start_service(handles_battery[IDX_SVC_BATTERY]); // trigger ESP_GATTS_START_EVT
            } else if (EXAMPLE_SVC_INST_ID_TEST == param->add_attr_tab.svc_inst_id) {
                ESP_LOGI(TAG, "0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X", param->add_attr_tab.handles[0], param->add_attr_tab.handles[1],
                    param->add_attr_tab.handles[2], param->add_attr_tab.handles[3], param->add_attr_tab.handles[4], param->add_attr_tab.handles[5]);
                memcpy(handles_test, param->add_attr_tab.handles, sizeof(uint16_t) * param->add_attr_tab.num_handle);
                esp_ble_gatts_start_service(handles_test[IDX_SVC_TEST]);                  
            } else {
                ESP_LOGW(TAG, "unknown svc_inst_id:%u", param->add_attr_tab.svc_inst_id);
            }
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(TAG, "GATTS_START_EVT, service_handle:0x%04X status:0x%02X", param->start.service_handle, param->start.status);
            break;
        case ESP_GATTS_READ_EVT: // received ATT_READ_REQ, driver read chara value from db and send ATT_READ_RSP auto, then trigger ESP_GATTS_READ_EVT
            ESP_LOGI(TAG, "GATTS_READ_EVT, addr:%02X:%02X:%02X:%02X:%02X:%02X conn_id:0x%04X handle:0x%04X is_long:%u offset:%u need_rsp:%u",
                param->read.bda[0], param->read.bda[1], param->read.bda[2], param->read.bda[3], param->read.bda[4], param->read.bda[5],
                param->read.conn_id, param->read.handle, param->read.is_long, param->read.offset, param->read.need_rsp);
            if (handles_battery[IDX_CHAR_VAL_BATTERY_LEVEL] == param->read.handle) {
                char_val_battery_level++; // just write chara value to memory, not write to db
                esp_ble_gatts_set_attr_value(handles_battery[IDX_CHAR_VAL_BATTERY_LEVEL], 1, &char_val_battery_level); // write chara value to db
            }
       	    break;
        case ESP_GATTS_WRITE_EVT: // received ATT_WRITE_REQ/CMD, driver write chara value to db and send ATT_WRITE_RSP auto, then trigger ESP_GATTS_WRITE_EVT
            ESP_LOGI(TAG, "GATTS_WRITE_EVT, addr:%02X:%02X:%02X:%02X:%02X:%02X conn_id:0x%04X handle:0x%04X is_prep:%u offset:%u need_rsp:%u len:%u value:",
                param->write.bda[0], param->write.bda[1], param->write.bda[2], param->write.bda[3], param->write.bda[4], param->write.bda[5],
                param->write.conn_id, param->write.handle, param->write.is_prep, param->write.offset, param->write.need_rsp, param->write.len);
            esp_log_buffer_hex(TAG, param->write.value, param->write.len);          
            if (handles_test[IDX_CHAR_CCC_TX] == param->write.handle && param->write.len == sizeof(char_ccc_tx)) {
                char_ccc_tx = param->write.value[1] << 8 | param->write.value[0]; // just write chara value to memory, already write to db
                if (char_ccc_tx == 0x0000) {
                    ESP_LOGI(TAG, "tx notify/indicate disable");
                } else if (char_ccc_tx == 0x0001) {
                    ESP_LOGI(TAG, "tx notify enable");
                } else if (char_ccc_tx == 0x0002) {
                    ESP_LOGI(TAG, "tx indicate enable");
                } else if (char_ccc_tx == 0x0003) {
                    ESP_LOGI(TAG, "tx notify/indicate enable");
                } else {
                    ESP_LOGE(TAG, "unknown ccc value");
                }
            } else { 
                memset(char_val_rx, 0, sizeof(char_val_rx));
                memcpy(char_val_rx, param->write.value, param->write.len); // just write chara value to memory, already write to db
                if (param->write.value[0] == 0x55) {
                    for (uint8_t i = 0; i < sizeof(char_val_tx); i++) {
                        char_val_tx[i]++;
                    }
                    // send ATT_HANDLE_VALUE_NTF, not send if char_ccc_tx tx notify disable
                    esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, handles_test[IDX_CHAR_VAL_TX], sizeof(char_val_tx), char_val_tx, false);                   
                }
            }
      	    break;
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "GATTS_CONNECT_EVT, addr:%02X:%02X:%02X:%02X:%02X:%02X conn_id:0x%04X interval:%u latency:%u timeout:%u",
                param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2], param->connect.remote_bda[3], param->connect.remote_bda[4],
                param->connect.remote_bda[5], param->connect.conn_id, param->connect.conn_params.interval, param->connect.conn_params.latency, param->connect.conn_params.timeout);
            esp_ble_conn_update_params_t conn_params = { 0 };
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.latency = 0;
            conn_params.min_int = 0x10;     // min_int = 0x10*1.25ms = 20ms
            conn_params.max_int = 0x20;     // max_int = 0x20*1.25ms = 40ms
            conn_params.timeout = 400;      // timeout = 400*10ms = 4000ms
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "GATTS_DISCONNECT_EVT, addr:%02X:%02X:%02X:%02X:%02X:%02X conn_id:0x%04X reason:0x%02X",
                param->disconnect.remote_bda[0], param->disconnect.remote_bda[1], param->disconnect.remote_bda[2], param->disconnect.remote_bda[3],
                param->disconnect.remote_bda[4], param->disconnect.remote_bda[5], param->disconnect.conn_id, param->disconnect.reason);
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GATTS_MTU_EVT: // received ATT_EXCHANGE_MTU_REQ, driver update ATT_MTU and send ATT_EXCHANGE_MTU_RSP auto, then trigger ESP_GATTS_MTU_EVT
            ESP_LOGI(TAG, "GATTS_MTU_EVT, conn_id:0x%04X mtu:%u", param->mtu.conn_id, param->mtu.mtu);
            break;
        case ESP_GATTS_CONF_EVT:
            ESP_LOGI(TAG, "GATTS_CONF_EVT, conn_id:0x%04X handle:0x%04X status:0x%02X len:%u", param->conf.conn_id, param->conf.handle, param->conf.status, param->conf.len);
            esp_log_buffer_hex(TAG, param->conf.value, param->conf.len);
            break;
        case ESP_GATTS_RESPONSE_EVT:
            ESP_LOGI(TAG, "GATTS_RESPONSE_EVT, handle:0x%04X status:0x%02X", param->rsp.handle, param->rsp.status);
            break;
        case ESP_GATTS_SET_ATTR_VAL_EVT:
            ESP_LOGI(TAG, "GATTS_SET_ATTR_VAL_EVT, srvc_handle:0x%04X attr_handle:0x%04X status:0x%02X",
                param->set_attr_val.srvc_handle, param->set_attr_val.attr_handle, param->set_attr_val.status);
            break;
        default:
            ESP_LOGW(TAG, "unknown event:%u", event);
            break;
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

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(EXAMPLE_APP_ID); // trigger ESP_GATTS_REG_EVT
    esp_ble_gatt_set_local_mtu(500);
}
