#ifndef LOG_SAMPLE_H_
#define LOG_SAMPLE_H_

#include <stdint.h>
#include "esp_http_server.h"

esp_err_t http_log_sample_handler(httpd_req_t *req);

#endif
