#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "mdns.h"

static const char *TAG = "mdns";
static int sock = 0;
static uint8_t req[256] = {0};
static uint8_t resp[1024] = {0};


esp_err_t mdns_init() {
    int err = 0;
    struct sockaddr_in local_addr = {0};
    uint8_t ttl = 3;
    uint8_t loopback = 0;
    struct ip_mreq imreq = {0};
    struct timeval tv = {
        .tv_sec = MDNS_UPD_RECV_TIMEOUT / 1000,
        .tv_usec = (MDNS_UPD_RECV_TIMEOUT % 1000) * 1000
    };

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed:%d", errno);
        goto exit;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(MDNS_UPD_PORT);
    err = bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "socket bind failed:%d", errno);
        goto exit;
    }

    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    if (err < 0) {
        ESP_LOGE(TAG, "socket set IP_MULTICAST_TTL failed:%d", errno);
        goto exit;
    }

    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback));
    if (err < 0) {
        ESP_LOGE(TAG, "socket set IP_MULTICAST_LOOP failed:%d", errno);
        goto exit;
    }

    imreq.imr_multiaddr.s_addr = inet_addr(MDNS_UPD_IP);
    imreq.imr_interface.s_addr = htonl(INADDR_ANY);
    err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(imreq));
    if (err < 0) {
        ESP_LOGE(TAG, "socket set IP_ADD_MEMBERSHIP failed:%d", errno);
        goto exit;
    }

    err = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (err < 0) {
        ESP_LOGE(TAG, "socket set SO_RCVTIMEO failed:%d", errno);
        goto exit;
    }

    ESP_LOGI(TAG, "mdns socket init success");
    return ESP_OK;

exit:
    ESP_LOGE(TAG, "mdns socket init failed:%d", err);
    return err;
}

esp_err_t mdns_query_ptr(char *srv_type, char *trans_type, mdns_result_ptr_t *result) {
    uint32_t req_len = 0, i = 0;
    int resp_len = 0;
    char *domain = "local";
    struct sockaddr_in remote_addr = {0};
    socklen_t addr_len = sizeof(remote_addr);

    req[req_len++] = 0;
    req[req_len++] = 0; // trans_id
    req[req_len++] = 0;
    req[req_len++] = 0; // flags: standard query
    req[req_len++] = 0;
    req[req_len++] = 1; // question
    req[req_len++] = 0;
    req[req_len++] = 0; // answer
    req[req_len++] = 0;
    req[req_len++] = 0; // authority
    req[req_len++] = 0;
    req[req_len++] = 0; // additional

    req[req_len++] = strlen(srv_type);
    memcpy(&req[req_len], srv_type, strlen(srv_type));
    req_len += strlen(srv_type);
    req[req_len++] = strlen(trans_type);
    memcpy(&req[req_len], trans_type, strlen(trans_type));
    req_len += strlen(trans_type);
    req[req_len++] = strlen(domain);
    memcpy(&req[req_len], domain, strlen(domain));
    req_len += strlen(domain);
    req[req_len++] = 0;

    req[req_len++] = 0;
    req[req_len++] = MDNS_QUERY_TYPE_PTR; // type
    req[req_len++] = 0;
    req[req_len++] = 0x01; // class, multicase response

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr(MDNS_UPD_IP);
    remote_addr.sin_port = htons(MDNS_UPD_PORT);
    sendto(sock, req, req_len, 0, (struct sockaddr *)&remote_addr, addr_len);

    resp_len = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr *)&remote_addr, &addr_len);
    if (resp_len < 0) {
        ESP_LOGE(TAG, "socket recv timeout");
        return ESP_ERR_TIMEOUT;
    }

    i = 12; // skip trans_id(2B), flags(2B), question(2B), answer(2B), authority(2B), additional(2B)
    while (resp[i]) { // skip name
        i += resp[i] + 1;
    }
    i++;
    i += 10; // skip type(2B), class(2B), TTL(4B), length(2B)

    memcpy(result->instance, &resp[i + 1], resp[i]);

    return ESP_OK;
}

esp_err_t mdns_query_srv(char *instance_name, char *srv_type, char *trans_type, mdns_result_srv_t *result) {
    uint32_t req_len = 0, i = 0;
    int resp_len = 0;
    char *domain = "local";
    struct sockaddr_in remote_addr = {0};
    socklen_t addr_len = sizeof(remote_addr);
    uint32_t target_len = 0, offset = 0;

    req[req_len++] = 0;
    req[req_len++] = 0; // trans_id
    req[req_len++] = 0;
    req[req_len++] = 0; // flags: standard query
    req[req_len++] = 0;
    req[req_len++] = 1; // question
    req[req_len++] = 0;
    req[req_len++] = 0; // answer
    req[req_len++] = 0;
    req[req_len++] = 0; // authority
    req[req_len++] = 0;
    req[req_len++] = 0; // additional

    req[req_len++] = strlen(instance_name);
    memcpy(&req[req_len], instance_name, strlen(instance_name));
    req_len += strlen(instance_name);
    req[req_len++] = strlen(srv_type);
    memcpy(&req[req_len], srv_type, strlen(srv_type));
    req_len += strlen(srv_type);
    req[req_len++] = strlen(trans_type);
    memcpy(&req[req_len], trans_type, strlen(trans_type));
    req_len += strlen(trans_type);
    req[req_len++] = strlen(domain);
    memcpy(&req[req_len], domain, strlen(domain));
    req_len += strlen(domain);
    req[req_len++] = 0;

    req[req_len++] = 0;
    req[req_len++] = MDNS_QUERY_TYPE_SRV; // type
    req[req_len++] = 0;
    req[req_len++] = 0x01; // class, multicase response

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr(MDNS_UPD_IP);
    remote_addr.sin_port = htons(MDNS_UPD_PORT);
    sendto(sock, req, req_len, 0, (struct sockaddr *)&remote_addr, addr_len);

    resp_len = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr *)&remote_addr, &addr_len);
    if (resp_len < 0) {
        ESP_LOGE(TAG, "socket recv timeout");
        return ESP_ERR_TIMEOUT;
    }

    i = 12; // skip trans_id(2B), flags(2B), question(2B), answer(2B), authority(2B), additional(2B)
    while (resp[i]) { // skip name
        i += resp[i] + 1;
    }
    i++;
    i += 10; // skip type(2B), class(2B), TTL(4B), length(2B)

    result->priority = resp[i] << 8 | resp[i + 1];
    result->weight = resp[i + 2] << 8 | resp[i + 3];
    result->port = resp[i + 4] << 8 | resp[i + 5];
    if (0xc0 == resp[i + 6]) {
        offset = resp[i + 7];
    } else {
        offset = i + 6;
    }
    while (resp[offset]) {
        memcpy(result->target + target_len, &resp[offset + 1], resp[offset]);
        target_len += resp[offset];
        result->target[target_len] = '.';
        target_len += 1;
        offset += resp[offset] + 1;
    }
    result->target[target_len - 1] = 0; // delete the last char "."

    return ESP_OK;
}

esp_err_t mdns_query_txt(char *instance_name, char *srv_type, char *trans_type, mdns_result_txt_t *result, uint32_t *cnt) {
    uint32_t req_len = 0, i = 0;
    int resp_len = 0;
    char *domain = "local";
    struct sockaddr_in remote_addr = {0};
    socklen_t addr_len = sizeof(remote_addr);
    uint16_t total_data_len = 0, data_len = 0, kv_len = 0;
    char kv[CONFIG_MDNS_MAX_TXT_KEY_SIZE + CONFIG_MDNS_MAX_TXT_VALUE_SIZE] = {0};
    uint32_t kv_cnt = 0;
    char *equal = NULL;

    req[req_len++] = 0;
    req[req_len++] = 0; // trans_id
    req[req_len++] = 0;
    req[req_len++] = 0; // flags: standard query
    req[req_len++] = 0;
    req[req_len++] = 1; // question
    req[req_len++] = 0;
    req[req_len++] = 0; // answer
    req[req_len++] = 0;
    req[req_len++] = 0; // authority
    req[req_len++] = 0;
    req[req_len++] = 0; // additional

    req[req_len++] = strlen(instance_name);
    memcpy(&req[req_len], instance_name, strlen(instance_name));
    req_len += strlen(instance_name);
    req[req_len++] = strlen(srv_type);
    memcpy(&req[req_len], srv_type, strlen(srv_type));
    req_len += strlen(srv_type);
    req[req_len++] = strlen(trans_type);
    memcpy(&req[req_len], trans_type, strlen(trans_type));
    req_len += strlen(trans_type);
    req[req_len++] = strlen(domain);
    memcpy(&req[req_len], domain, strlen(domain));
    req_len += strlen(domain);
    req[req_len++] = 0;

    req[req_len++] = 0;
    req[req_len++] = MDNS_QUERY_TYPE_TXT; // type
    req[req_len++] = 0;
    req[req_len++] = 0x01; // class, multicase response

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr(MDNS_UPD_IP);
    remote_addr.sin_port = htons(MDNS_UPD_PORT);
    sendto(sock, req, req_len, 0, (struct sockaddr *)&remote_addr, addr_len);

    resp_len = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr *)&remote_addr, &addr_len);
    if (resp_len < 0) {
        ESP_LOGE(TAG, "socket recv timeout");
        return ESP_ERR_TIMEOUT;
    }

    i = 12; // skip trans_id(2B), flags(2B), question(2B), answer(2B), authority(2B), additional(2B)
    while (resp[i]) { // skip name
        i += resp[i] + 1;
    }
    i++;
    i += 8; // skip type(2B), class(2B), TTL(4B)

    total_data_len = resp[i] << 8 | resp[i + 1];
    while (data_len < total_data_len) {
        kv_len = resp[i + 2];
        memcpy(kv, &resp[i + 3], kv_len);
        kv[kv_len] = 0;
        equal = strchr(kv, '=');
        memcpy(result[kv_cnt].key, kv, equal - kv);
        memcpy(result[kv_cnt].value, equal + 1, kv_len - (equal - kv + 1));
        kv_cnt++;
        data_len += kv_len + 1;
        i += kv_len + 1;
    }
    *cnt = kv_cnt;

    return ESP_OK;
}
