#ifndef IR_NEC_ENCODER_H_
#define IR_NEC_ENCODER_H_

#include <stdint.h>
#include "driver/rmt_encoder.h"

typedef struct {
    uint16_t address;
    uint16_t command;
} ir_nec_frame_t;

esp_err_t rmt_new_ir_nec_encoder(uint32_t resolution, rmt_encoder_handle_t *ret_encoder);

#endif
