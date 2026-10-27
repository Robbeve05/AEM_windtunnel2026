/* Author: R.E. van Eekelen */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool I2CBus_Init(void);
bool I2CBus_Write(uint8_t address,const uint8_t *data,size_t size);
bool I2CBus_Read(uint8_t address,uint8_t *data,size_t size);
bool DigitalOutputs_Set(bool enabled);

/* Called exclusively by Core 1: zero-wait mutex and bounded I2C timeout. */
bool I2CBus_TryRead(uint8_t address, uint8_t *data, size_t size);
/* Core 0 timed pressure-tap scanner: one bounded write per phase. */
bool I2CBus_TryWrite(uint8_t address, const uint8_t *data, size_t size);
