#ifndef BUTTON_ID_H_
#define BUTTON_ID_H_

#include <stdint.h>
#include "ir_nec_encoder.h"

#define EXAMPLE_IR_TX_GPIO_NUM              18

void ir_rmt_init();
void ir_rmt_send(ir_nec_frame_t frame);
void ir_rmt_recv(uint8_t rmt_id, uint8_t channel_id);

#endif
