#include <stdint.h>
#include "esp_err.h"

#define MDNS_UPD_IP                         "224.0.0.251"
#define MDNS_UPD_PORT                       5353
#define MDNS_UPD_RECV_TIMEOUT               3000
#define CONFIG_MDNS_MAX_NAME_SIZE           64
#define CONFIG_MDNS_MAX_TXT_KEY_SIZE        32
#define CONFIG_MDNS_MAX_TXT_VALUE_SIZE      64


typedef enum {
    MDNS_QUERY_TYPE_A = 0x01,
    MDNS_QUERY_TYPE_PTR = 0x0C,
    MDNS_QUERY_TYPE_TXT = 0x10,
    MDNS_QUERY_TYPE_AAAA = 0x1C,
    MDNS_QUERY_TYPE_SRV = 0x21,
} mdns_query_type_t;

typedef struct {
    char instance[CONFIG_MDNS_MAX_NAME_SIZE];
} mdns_result_ptr_t;

typedef struct {
    uint16_t priority;
    uint16_t weight;
    uint16_t port;
    char target[CONFIG_MDNS_MAX_NAME_SIZE];
} mdns_result_srv_t;

typedef struct {
    char key[CONFIG_MDNS_MAX_TXT_KEY_SIZE];
    char value[CONFIG_MDNS_MAX_TXT_VALUE_SIZE];
} mdns_result_txt_t;

typedef struct {
    char ipv4[4];
} mdns_result_a_t;

typedef struct {
	char ipv6[16];
} mdns_record_aaaa_t;

esp_err_t mdns_init();
esp_err_t mdns_query_ptr(char *srv_type, char *trans_type, mdns_result_ptr_t *result);
esp_err_t mdns_query_srv(char *instance_name, char *srv_type, char *trans_type, mdns_result_srv_t *result);
esp_err_t mdns_query_txt(char *instance_name, char *srv_type, char *trans_type, mdns_result_txt_t *result, uint32_t *cnt);
