#include <stdint.h>
#include "esp_err.h"

#define MDNS_UPD_IP                                 "224.0.0.251"
#define MDNS_UPD_PORT                               5353
#define CONFIG_MDNS_RECV_TIMEOUT                    3000
#define CONFIG_MDNS_TTL                             4500
#define CONFIG_MDNS_MAX_SERVICE                     10
#define CONFIG_MDNS_SERVICE_MAX_TXT                 5


typedef enum {
    MDNS_QUERY_TYPE_A = 0x01,
    MDNS_QUERY_TYPE_PTR = 0x0C,
    MDNS_QUERY_TYPE_TXT = 0x10,
    MDNS_QUERY_TYPE_AAAA = 0x1C,
    MDNS_QUERY_TYPE_SRV = 0x21,
} mdns_query_type_t;

typedef struct {
    char ins_name[32];
} mdns_ptr_t;

typedef struct {
    uint16_t priority;
    uint16_t weight;
    uint16_t port;
    char target[64];
} mdns_srv_t;

typedef struct {
    char key[16];
    char value[32];
} mdns_txt_t;

typedef struct {
    uint8_t ip[4];
} mdns_a_t;

typedef struct {
	uint8_t ip[16];
} mdns_aaaa_t;

typedef struct {
    char ins_name[32];
    char srv_type[32];
    char trans_type[16];
    uint16_t priority;
    uint16_t weight;
    uint16_t port;
    mdns_txt_t txt[CONFIG_MDNS_SERVICE_MAX_TXT];
    uint8_t txt_cnt;
    mdns_a_t ipV4;
    mdns_aaaa_t ipV6;
} mdns_service_t;

esp_err_t mdns_init();
esp_err_t mdns_query_ptr(char *srv_type, char *trans_type, mdns_ptr_t *result);
esp_err_t mdns_query_srv(char *ins_name, char *srv_type, char *trans_type, mdns_srv_t *result);
esp_err_t mdns_query_txt(char *ins_name, char *srv_type, char *trans_type, mdns_txt_t *result, uint32_t *cnt);
esp_err_t mdns_query_a(char *ins_name, char *srv_type, char *trans_type, mdns_a_t *result);
esp_err_t mdns_query_aaaa(char *ins_name, char *srv_type, char *trans_type, mdns_aaaa_t *result);
esp_err_t mdns_add_srv(char *ins_name, char *srv_type, char *trans_type, uint16_t port,
                       mdns_a_t *ipV4, mdns_aaaa_t *ipV6, mdns_txt_t *txt, uint32_t cnt);
esp_err_t mdns_start_server();