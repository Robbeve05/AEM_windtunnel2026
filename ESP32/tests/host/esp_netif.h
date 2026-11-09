#pragma once
#include <stdint.h>
typedef struct esp_netif esp_netif_t;
typedef struct { struct { uint32_t addr; } ip, netmask, gw; } esp_netif_ip_info_t;
