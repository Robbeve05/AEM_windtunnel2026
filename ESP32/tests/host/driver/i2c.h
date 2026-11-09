#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#define I2C_MODE_MASTER 1
typedef struct {
    int mode, sda_io_num, scl_io_num;
    bool sda_pullup_en, scl_pullup_en;
    struct { unsigned clk_speed; } master;
} i2c_config_t;
esp_err_t i2c_param_config(int port, const i2c_config_t *config);
esp_err_t i2c_driver_install(int port, int mode, size_t rx, size_t tx, int flags);
esp_err_t i2c_master_read_from_device(int port, uint8_t address, uint8_t *data, size_t size, TickType_t wait);
esp_err_t i2c_master_write_to_device(int port, uint8_t address, const uint8_t *data, size_t size, TickType_t wait);
