/* Author: R.E. van Eekelen. Host-only RMT encoder interface. */
#pragma once
#include "esp_err.h"
typedef void *rmt_encoder_handle_t;
typedef struct { unsigned level0, duration0, level1, duration1; } HostRmtBit;
typedef struct {
    HostRmtBit bit0, bit1;
    struct { unsigned msb_first; } flags;
} rmt_bytes_encoder_config_t;
esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *, rmt_encoder_handle_t *);
