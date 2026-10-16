#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* One bounded RTU exchange. Transport returns the complete response length. */
typedef size_t (*V20Exchange)(void *, const uint8_t *, size_t, uint8_t *, size_t);
typedef struct {
    V20Exchange exchange;
    void *context;
    uint8_t slave;
    bool configured, stop_required;
    uint16_t status;
    float actual_hz;
} V20;
uint16_t V20_Crc(const uint8_t *data, size_t length);
bool V20_Init(V20 *drive, uint8_t slave, V20Exchange exchange, void *context);
/* Explicit run flag: zero frequency is not a stop command. Faults are never reset automatically. */
bool V20_Write(V20 *drive, bool run, float percent);
