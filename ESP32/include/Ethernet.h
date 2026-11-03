#pragma once

#include <stdbool.h>
#include "esp_netif.h"

void Ethernet_Init(void);
bool Ethernet_LinkUp(void);
bool Ethernet_HasIp(void);
bool Ethernet_GetIpInfo(esp_netif_ip_info_t *info);
