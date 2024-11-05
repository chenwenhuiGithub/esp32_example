#ifndef IR_H_
#define IR_H_

#include <stdint.h>

#define RMTID_TV                            0
#define RMTID_SETTOPBOX                     1
#define RMTID_AC_LIVING                     2
#define RMTID_AC_BEDROOM                    3
#define RMTID_LIGHT_BEDROOM                 4
#define RMTID_MAX                           5

#define CHANNELID_0                         0
#define CHANNELID_1                         1
#define CHANNELID_2                         2
#define CHANNELID_3                         3
#define CHANNELID_4                         4
#define CHANNELID_5                         5
#define CHANNELID_6                         6
#define CHANNELID_7                         7
#define CHANNELID_8                         8
#define CHANNELID_9                         9
#define CHANNELID_UP                        10
#define CHANNELID_DOWN                      11
#define CHANNELID_LEFT                      12
#define CHANNELID_RIGHT                     13
#define CHANNELID_OK                        14
#define CHANNELID_VOLUME_ADD                15
#define CHANNELID_VOLUME_SUB                16
#define CHANNELID_CHANNEL_ADD               17
#define CHANNELID_CHANNEL_SUB               18
#define CHANNELID_POWER                     19
#define CHANNELID_HOME                      20
#define CHANNELID_SIGNAL                    21
#define CHANNELID_MUTE                      22
#define CHANNELID_BACK                      23
#define CHANNELID_MENU                      24
#define CHANNELID_SETTING                   25
#define CHANNELID_MAX                       64

#define CONFIG_GPIO_NUM_IR_TX               18
#define CONFIG_GPIO_NUM_IR_RX               19


void ir_init();
void ir_send(uint8_t *data, uint32_t len);
void ir_recv(uint8_t rmt_id, uint8_t channel_id);

#endif
