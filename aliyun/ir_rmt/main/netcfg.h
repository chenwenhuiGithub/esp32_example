#ifndef NETCFG_H_
#define NETCFG_H_

#include <stdint.h>

#define CONFIG_GPIO_NUM_NETCFG_LED                      2

typedef enum {
    NETSTAT_WIFI_NOT_CONNECTED,
    NETSTAT_WIFI_CONNECTED,
    NETSTAT_CLOUD_CONNECTED
} netcfg_netstat_t;

void netcfg_init();
void netcfg_get_netstat(netcfg_netstat_t *stat);
void netcfg_set_netstat(netcfg_netstat_t stat);
void netcfg_get_wifi_info(char *ssid, char *pwd);
void netcfg_set_wifi_info(char *ssid, char *pwd);

#endif
