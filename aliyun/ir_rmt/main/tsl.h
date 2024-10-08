#ifndef TSL_H_
#define TSL_H_

#include <stdint.h>

void tsl_recv_set(uint8_t *payload, uint32_t len);
void tsl_recv_post_reply(uint8_t *payload, uint32_t len);
void tsl_send_set_reply(char *msg_id);
void tsl_send_post();

#endif
