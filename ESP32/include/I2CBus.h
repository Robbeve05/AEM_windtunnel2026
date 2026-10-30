/* Author: R.E. van Eekelen */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool I2CBus_Init(void);
bool I2CBus_TryRead(uint8_t address,uint8_t *data,size_t size);
bool I2CBus_TryWrite(uint8_t address,const uint8_t *data,size_t size);
bool I2CBus_Write(uint8_t address,const uint8_t *data,size_t size);
bool I2CBus_Read(uint8_t address,uint8_t *data,size_t size);
#include "DigitalOutputs.h" // Existing users retain DigitalOutputs_Set compatibility.
