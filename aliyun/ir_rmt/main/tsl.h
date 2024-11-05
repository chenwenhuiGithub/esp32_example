#ifndef TSL_H_
#define TSL_H_

#include <stdint.h>

void tsl_recv_set(uint8_t *payload, uint32_t len);
void tsl_send_post();

#endif
