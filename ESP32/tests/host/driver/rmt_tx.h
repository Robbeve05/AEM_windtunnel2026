/* Author: R.E. van Eekelen. Host-only asynchronous RMT interface. */
#pragma once
#include <stddef.h>
#include "rmt_encoder.h"
#define RMT_CLK_SRC_DEFAULT 0
typedef void *rmt_channel_handle_t;
typedef struct {
    int gpio_num, clk_src;
    unsigned resolution_hz, mem_block_symbols, trans_queue_depth;
} rmt_tx_channel_config_t;
typedef struct { int loop_count; } rmt_transmit_config_t;
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void *, size_t,
                       const rmt_transmit_config_t *);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int);
