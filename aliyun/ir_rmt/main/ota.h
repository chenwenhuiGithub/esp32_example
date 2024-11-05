#ifndef OTA_H_
#define OTA_H_

#include <stdint.h>
#include "esp_http_server.h"

#define CONFIG_OTA_REPORT_PROGRESS_PERIOD           3000 // 3s

void ota_report_version();
void ota_start(uint8_t *payload, uint32_t len);
esp_err_t http_ota_update_handler(httpd_req_t *req);

#endif
