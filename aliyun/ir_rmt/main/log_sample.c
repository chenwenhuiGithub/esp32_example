#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "log_sample.h"

static int s_sock = 0;
static char log_buf[256] = {0};
static const char *TAG = "log_sample";

static int log_sample_vprintf(const char * format, va_list args)
{
    size_t len = vsnprintf(log_buf, sizeof(log_buf), format, args);
    if (len > sizeof(log_buf) - 1) {
        log_buf[sizeof(log_buf) - 1] = 0;
    }
    send(s_sock, log_buf, strlen(log_buf), 0);
    return len;
}

static int log_sample_start(char *ip, uint16_t port) {
    int err = 0;
    struct sockaddr_in server_addr = { 0 };

    s_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);
    ESP_LOGI(TAG, "tcp client start connect %s:%u", ip, port);
    err = connect(s_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket connect failed:%d", errno);
        return 2;
    }
    ESP_LOGI(TAG, "socket connect success");

    esp_log_set_vprintf(&log_sample_vprintf);
    
    return 0;
}

static void log_sample_stop() {
    esp_log_set_vprintf(&vprintf);
    close(s_sock);
}

esp_err_t http_log_sample_handler(httpd_req_t *req) {
    char post_data[128] = {0};
    cJSON *root = NULL;
    char ip[32] = {0};
    uint16_t port = 0;
    uint8_t sample = 0;
    int ret = 0;

    httpd_req_recv(req, post_data, sizeof(post_data));
    ESP_LOGI(TAG, "http post data:%s", post_data);

    root = cJSON_Parse(post_data);
    memcpy(ip, cJSON_GetObjectItem(root, "ip")->valuestring, strlen(cJSON_GetObjectItem(root, "ip")->valuestring));
    port = cJSON_GetObjectItem(root, "port")->valueint;
    sample = cJSON_GetObjectItem(root, "sample")->valueint;
    cJSON_Delete(root);

    if (sample) {
        ret = log_sample_start(ip, port);
        httpd_resp_set_status(req, HTTPD_200);
        if (0 == ret) {
            httpd_resp_send(req, "{\"code\":0, \"message\":\"success\"}", strlen("\"code\":0, {\"message\":\"success\"}"));
        } else if (-1 == ret) {
            httpd_resp_send(req, "{\"code\":1, \"message\":\"socket create failed\"}", strlen("\"code\":1, {\"message\":\"socket create failed\"}"));
        } else if (-2 == ret) {
            httpd_resp_send(req, "{\"code\":2, \"message\":\"socket connect failed\"}", strlen("\"code\":2, {\"message\":\"socket connect failed\"}"));
        }
    } else {
        log_sample_stop();
        httpd_resp_send(req, "{\"code\":0, \"message\":\"success\"}", strlen("\"code\":0, {\"message\":\"success\"}"));
    }

    return ESP_OK;
}
