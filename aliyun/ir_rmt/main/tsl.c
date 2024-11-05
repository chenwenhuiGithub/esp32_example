#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "cloud.h"
#include "tsl.h"
#include "ir.h"


static uint8_t rmt_id = 0;
static uint8_t channel_id = 0;
static const char *TAG = "tsl";

void tsl_recv_set(uint8_t *payload, uint32_t len) {
    cJSON *root = cJSON_Parse((char *)payload);
    // cJSON *id_json = cJSON_GetObjectItem(root, "id");
    cJSON *param_json = cJSON_GetObjectItem(root, "params");
    cJSON *rmtId_json = cJSON_GetObjectItem(param_json, "rmtId");
    cJSON *channelId_json = cJSON_GetObjectItem(param_json, "channelId");
    uint8_t has_channelId = 0;
    // char set_reply[256] = {0};

    // sprintf(set_reply, "{\"code\":200, \"data\":{}, \"id\":\"%s\", \"message\":\"success\", \"version\":\"1.0.0\"}", id_json->valuestring);
    // cloud_send_publish(CONFIG_TOPIC_TSL_SET_REPLY, (uint8_t *)set_reply, strlen(set_reply));

    if (rmtId_json) {
        rmt_id = rmtId_json->valueint;
    }
    if (channelId_json) {
        channel_id = channelId_json->valueint;
        has_channelId = 1;
    }
    cJSON_Delete(root);

    if (!has_channelId) {
        ESP_LOGI(TAG, "rmt_id:%u", rmt_id);
    } else {
        ESP_LOGI(TAG, "rmt_id:%u, channel_id:%u", rmt_id, channel_id);
        ir_recv(rmt_id, channel_id);
    }
}

void tsl_send_post() {
    cJSON *root = cJSON_CreateObject();
    cJSON *param_json = cJSON_CreateObject();
    char *buf = NULL;

    cJSON_AddStringToObject(root, "id", cloud_gen_msg_id());
    cJSON_AddStringToObject(root, "version", "1.0.0");
    cJSON_AddStringToObject(root, "method", "thing.event.property.post");
    cJSON_AddNumberToObject(param_json, "rmtId", rmt_id);
    cJSON_AddNumberToObject(param_json, "channelId", channel_id);
    cJSON_AddItemToObject(root, "params", param_json);
    buf = cJSON_PrintUnformatted(root);
    cloud_send_publish(CONFIG_TOPIC_TSL_POST, (uint8_t *)buf, strlen(buf));
    free(buf);
    cJSON_Delete(root);
}
