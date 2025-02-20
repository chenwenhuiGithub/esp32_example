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
static mdns_service_t g_srv[CONFIG_MDNS_MAX_SERVICE] = {0};
static uint8_t srv_cnt = 0;

esp_err_t mdns_init() {
    int err = 0;
    struct sockaddr_in local_addr = {0};
    uint8_t ttl = 3;
    uint8_t loopback = 0;
    struct ip_mreq imreq = {0};
    struct timeval tv = {
        .tv_sec = CONFIG_MDNS_RECV_TIMEOUT / 1000,
        .tv_usec = (CONFIG_MDNS_RECV_TIMEOUT % 1000) * 1000
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

esp_err_t mdns_query_ptr(char *srv_type, char *trans_type, mdns_ptr_t *result) {
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

    memcpy(result->ins_name, &resp[i + 1], resp[i]);

    return ESP_OK;
}

esp_err_t mdns_query_srv(char *ins_name, char *srv_type, char *trans_type, mdns_srv_t *result) {
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

    req[req_len++] = strlen(ins_name);
    memcpy(&req[req_len], ins_name, strlen(ins_name));
    req_len += strlen(ins_name);
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

esp_err_t mdns_query_txt(char *ins_name, char *srv_type, char *trans_type, mdns_txt_t *result, uint32_t *cnt) {
    uint32_t req_len = 0, i = 0;
    int resp_len = 0;
    char *domain = "local";
    struct sockaddr_in remote_addr = {0};
    socklen_t addr_len = sizeof(remote_addr);
    uint16_t total_data_len = 0, data_len = 0, kv_len = 0;
    char kv[64] = {0};
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

    req[req_len++] = strlen(ins_name);
    memcpy(&req[req_len], ins_name, strlen(ins_name));
    req_len += strlen(ins_name);
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

esp_err_t mdns_query_a(char *ins_name, char *srv_type, char *trans_type, mdns_a_t *result) {
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

    req[req_len++] = strlen(ins_name);
    memcpy(&req[req_len], ins_name, strlen(ins_name));
    req_len += strlen(ins_name);
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
    req[req_len++] = MDNS_QUERY_TYPE_A; // type
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

    memcpy(result->ip, &resp[i], 4);

    return ESP_OK;
}

esp_err_t mdns_query_aaaa(char *ins_name, char *srv_type, char *trans_type, mdns_aaaa_t *result) {
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

    req[req_len++] = strlen(ins_name);
    memcpy(&req[req_len], ins_name, strlen(ins_name));
    req_len += strlen(ins_name);
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
    req[req_len++] = MDNS_QUERY_TYPE_AAAA; // type
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

    memcpy(result->ip, &resp[i], 16);

    return ESP_OK;
}

esp_err_t mdns_add_srv(char *ins_name, char *srv_type, char *trans_type, uint16_t port,
                       mdns_a_t *ipV4, mdns_aaaa_t *ipV6, mdns_txt_t *txt, uint32_t cnt) {
    uint32_t i = 0;

    if (CONFIG_MDNS_MAX_SERVICE == srv_cnt) {
        ESP_LOGE(TAG, "service is full, max cnt:%u", srv_cnt);
        return -1;
    }

    memcpy(g_srv[srv_cnt].ins_name, ins_name, strlen(ins_name));
    memcpy(g_srv[srv_cnt].srv_type, srv_type, strlen(srv_type));
    memcpy(g_srv[srv_cnt].trans_type, trans_type, strlen(trans_type));
    g_srv[srv_cnt].priority = 0;
    g_srv[srv_cnt].weight = 0;
    g_srv[srv_cnt].port = port;
    for (i = 0; i < cnt; i++) {
        memcpy(g_srv[srv_cnt].txt[i].key, txt[i].key, strlen(txt[i].key));
        memcpy(g_srv[srv_cnt].txt[i].value, txt[i].value, strlen(txt[i].value));
    }
    g_srv[srv_cnt].txt_cnt = cnt;
    if (ipV4) {
        memcpy(g_srv[srv_cnt].ipV4.ip, ipV4->ip, sizeof(ipV4->ip));
    }
    if (ipV6) {
        memcpy(g_srv[srv_cnt].ipV6.ip, ipV6->ip, sizeof(ipV6->ip));
    }

    srv_cnt++;

    return ESP_OK;
}

esp_err_t mdns_start_server() {
    struct sockaddr_in remote_addr = {0};
    socklen_t addr_len = sizeof(remote_addr);
    int req_len = 0;
    uint32_t resp_len = 0, i = 0, j = 0, k = 0;
    char ins_name[32] = {0};
    char srv_type[32] = {0};
    char trans_type[16] = {0};
    char *domain = "local";
    uint32_t ins_name_len = 0, srv_type_len = 0, trans_type_len = 0;
    uint16_t query_type = 0;
    uint8_t is_unicast_resp = 0;
    uint16_t data_len = 0;

    while (1) {
        req_len = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&remote_addr, &addr_len);
        if (req_len > 0) {
            if (req[2] & 0x80) { // response, not query
                continue;
            }

            i = 12; // skip trans_id(2B), flags(2B), question(2B), answer(2B), authority(2B), additional(2B)
            while (req[i]) { // skip name
                i += req[i] + 1;
            }
            i++;

            query_type = (req[i] << 8) | req[i + 1];
            if (req[i + 2] & 0x80) {
                is_unicast_resp = 1;
            } else {
                is_unicast_resp = 0;
            }

            resp[0] = req[0];
            resp[1] = req[1]; // trans_id
            resp[2] = 0x84;
            resp[3] = 0x00;   // flags: standard query response
            resp[4] = 0;
            resp[5] = 0;      // question
            resp[6] = 0;
            resp[7] = 1;      // answer
            resp[8] = 0;
            resp[9] = 0;      // authority
            resp[10] = 0;
            resp[11] = 0;     // additional
            resp_len = 12;

            switch (query_type) {
            case MDNS_QUERY_TYPE_PTR:
                srv_type_len = req[12];
                memcpy(srv_type, &req[13], srv_type_len);
                trans_type_len = req[13 + srv_type_len];
                memcpy(trans_type, &req[14 + srv_type_len], trans_type_len);
                ESP_LOGI(TAG, "query_type:0x%04x srv_type:%s trans_type:%s", query_type, srv_type, trans_type);

                for (j = 0; j < srv_cnt; j++) {
                    if (strlen(g_srv[j].srv_type) == srv_type_len && strncmp(g_srv[j].srv_type, srv_type, srv_type_len) == 0) {
                        if (strlen(g_srv[j].trans_type) == trans_type_len && strncmp(g_srv[j].trans_type, trans_type, trans_type_len) == 0) {
                            resp[resp_len++] = srv_type_len;
                            memcpy(&resp[resp_len], srv_type, srv_type_len);
                            resp_len += srv_type_len;
                            resp[resp_len++] = trans_type_len;
                            memcpy(&resp[resp_len], trans_type, trans_type_len);
                            resp_len += trans_type_len;
                            resp[resp_len++] = strlen(domain);
                            memcpy(&resp[resp_len], domain, strlen(domain));
                            resp_len += strlen(domain);
                            resp[resp_len++] = 0;

                            resp[resp_len++] = query_type >> 8;
                            resp[resp_len++] = query_type;
                            resp[resp_len++] = 0x00;
                            resp[resp_len++] = 0x01; // class
                            resp[resp_len++] = CONFIG_MDNS_TTL >> 24;
                            resp[resp_len++] = CONFIG_MDNS_TTL >> 16;
                            resp[resp_len++] = CONFIG_MDNS_TTL >> 8;
                            resp[resp_len++] = (uint8_t)CONFIG_MDNS_TTL; // TTL
                            resp[resp_len++] = (strlen(g_srv[j].ins_name) + 3) >> 8;
                            resp[resp_len++] = strlen(g_srv[j].ins_name) + 3; // data_len
                            resp[resp_len++] = strlen(g_srv[j].ins_name);
                            memcpy(&resp[resp_len], g_srv[j].ins_name, strlen(g_srv[j].ins_name));
                            resp_len += strlen(g_srv[j].ins_name);
                            resp[resp_len++] = 0xc0;
                            resp[resp_len++] = 0x0c;

                            if (!is_unicast_resp) {
                                remote_addr.sin_family = AF_INET;
                                remote_addr.sin_addr.s_addr = inet_addr(MDNS_UPD_IP);
                                remote_addr.sin_port = htons(MDNS_UPD_PORT);
                            }                               
                            sendto(sock, resp, resp_len, 0, (struct sockaddr *)&remote_addr, addr_len);
                            break;
                        }
                    }
                }
                break;
            case MDNS_QUERY_TYPE_SRV:
            case MDNS_QUERY_TYPE_TXT:
            case MDNS_QUERY_TYPE_A:
                ins_name_len = req[12];
                memcpy(ins_name, &req[13], ins_name_len);
                srv_type_len = req[13 + ins_name_len];
                memcpy(srv_type, &req[14 + ins_name_len], srv_type_len);
                trans_type_len = req[14 + ins_name_len + srv_type_len];
                memcpy(trans_type, &req[15 + ins_name_len + srv_type_len], trans_type_len);
                ESP_LOGI(TAG, "query_type:0x%04x ins_name:%s srv_type:%s trans_type:%s", query_type, ins_name, srv_type, trans_type);

                for (j = 0; j < srv_cnt; j++) {
                    if (strlen(g_srv[j].ins_name) == ins_name_len && strncmp(g_srv[j].ins_name, ins_name, ins_name_len) == 0) {
                        if (strlen(g_srv[j].srv_type) == srv_type_len && strncmp(g_srv[j].srv_type, srv_type, srv_type_len) == 0) {
                            if (strlen(g_srv[j].trans_type) == trans_type_len && strncmp(g_srv[j].trans_type, trans_type, trans_type_len) == 0) {
                                resp[resp_len++] = ins_name_len;
                                memcpy(&resp[resp_len], ins_name, ins_name_len);
                                resp_len += ins_name_len;
                                resp[resp_len++] = srv_type_len;
                                memcpy(&resp[resp_len], srv_type, srv_type_len);
                                resp_len += srv_type_len;
                                resp[resp_len++] = trans_type_len;
                                memcpy(&resp[resp_len], trans_type, trans_type_len);
                                resp_len += trans_type_len;
                                resp[resp_len++] = strlen(domain);
                                memcpy(&resp[resp_len], domain, strlen(domain));
                                resp_len += strlen(domain);
                                resp[resp_len++] = 0;

                                resp[resp_len++] = query_type >> 8;
                                resp[resp_len++] = query_type;
                                resp[resp_len++] = 0x00;
                                resp[resp_len++] = 0x01; // class
                                resp[resp_len++] = CONFIG_MDNS_TTL >> 24;
                                resp[resp_len++] = CONFIG_MDNS_TTL >> 16;
                                resp[resp_len++] = CONFIG_MDNS_TTL >> 8;
                                resp[resp_len++] = (uint8_t)CONFIG_MDNS_TTL; // TTL

                                if (MDNS_QUERY_TYPE_SRV == query_type) {
                                    resp[resp_len++] = 0;
                                    resp[resp_len++] = 8; // data_len
                                    resp[resp_len++] = g_srv[j].priority >> 8;
                                    resp[resp_len++] = g_srv[j].priority;
                                    resp[resp_len++] = g_srv[j].weight >> 8;
                                    resp[resp_len++] = g_srv[j].weight;
                                    resp[resp_len++] = g_srv[j].port >> 8;
                                    resp[resp_len++] = g_srv[j].port;
                                    resp[resp_len++] = 0xc0;
                                    resp[resp_len++] = 0x0c;                                    
                                } else if (MDNS_QUERY_TYPE_TXT == query_type) {
                                    for (k = 0; k < g_srv[j].txt_cnt; k++) {
                                        data_len += 1;
                                        data_len += strlen(g_srv[j].txt[k].key) + 1 + strlen(g_srv[j].txt[k].value);
                                    }
                                    resp[resp_len++] = data_len >> 8;
                                    resp[resp_len++] = data_len; // data_len
                                    for (k = 0; k < g_srv[j].txt_cnt; k++) {
                                        resp[resp_len++] = strlen(g_srv[j].txt[k].key) + 1 + strlen(g_srv[j].txt[k].value);
                                        memcpy(&resp[resp_len], g_srv[j].txt[k].key, strlen(g_srv[j].txt[k].key));
                                        resp_len += strlen(g_srv[j].txt[k].key);
                                        resp[resp_len++] = '=';
                                        memcpy(&resp[resp_len], g_srv[j].txt[k].value, strlen(g_srv[j].txt[k].value));
                                        resp_len += strlen(g_srv[j].txt[k].value);
                                    }  
                                } else if (MDNS_QUERY_TYPE_A == query_type) {
                                    resp[resp_len++] = 0;
                                    resp[resp_len++] = 4; // data_len
                                    resp[resp_len++] = g_srv[j].ipV4.ip[3];
                                    resp[resp_len++] = g_srv[j].ipV4.ip[2];
                                    resp[resp_len++] = g_srv[j].ipV4.ip[1];
                                    resp[resp_len++] = g_srv[j].ipV4.ip[0];
                                }

                                if (!is_unicast_resp) {
                                    remote_addr.sin_family = AF_INET;
                                    remote_addr.sin_addr.s_addr = inet_addr(MDNS_UPD_IP);
                                    remote_addr.sin_port = htons(MDNS_UPD_PORT);
                                }                               
                                sendto(sock, resp, resp_len, 0, (struct sockaddr *)&remote_addr, addr_len);
                                break;
                            }
                        }
                    }
                }
                break;
            default:
                ESP_LOGW(TAG, "unsupported query type:0x%04x", query_type);
                break;
            }
        }        
    }
}
