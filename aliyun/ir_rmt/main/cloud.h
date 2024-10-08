#ifndef CLOUD_H_
#define CLOUD_H_

#include <stdint.h>

#define CONFIG_CLOUD_PK                             "a1GCY1V8kBX"
#define CONFIG_CLOUD_DK                             "ovHa9DNEP3ma1WZs6aNE"
#define CONFIG_CLOUD_DS                             "e36742c9698a83e63cf05c691a4bcc07"
#define CONFIG_MQTT_URL                             "mqtts://"CONFIG_CLOUD_PK".iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define CONFIG_MQTT_PORT                            1883
#define CONFIG_MQTT_KEEP_ALIVE                      300 // 5min
#define CONFIG_TOPIC_TSL_POST                       "/sys/"CONFIG_CLOUD_PK"/"CONFIG_CLOUD_DK"/thing/event/property/post"
#define CONFIG_TOPIC_TSL_POST_REPLY                 "/sys/"CONFIG_CLOUD_PK"/"CONFIG_CLOUD_DK"/thing/event/property/post_reply"
#define CONFIG_TOPIC_TSL_SET                        "/sys/"CONFIG_CLOUD_PK"/"CONFIG_CLOUD_DK"/thing/service/property/set"
#define CONFIG_TOPIC_TSL_SET_REPLY                  "/sys/"CONFIG_CLOUD_PK"/"CONFIG_CLOUD_DK"/thing/service/property/set_reply"
#define CONFIG_TOPIC_OTA_UPGRADE_TASK               "/ota/device/upgrade/"CONFIG_CLOUD_PK"/"CONFIG_CLOUD_DK
#define CONFIG_TOPIC_OTA_REPORT_PROGRESS            "/ota/device/progress/"CONFIG_CLOUD_PK"/"CONFIG_CLOUD_DK
#define CONFIG_TOPIC_OTA_REPORT_VERSION             "/ota/device/inform/"CONFIG_CLOUD_PK"/"CONFIG_CLOUD_DK
#define CONFIG_OTA_PERIOD_REPORT_PROGRESS           3000 // 3s

void cloud_start_connect();
void cloud_stop_connect();
void cloud_send_publish(char *topic, uint8_t *payload, uint32_t len);
char *cloud_gen_msg_id();

#endif
