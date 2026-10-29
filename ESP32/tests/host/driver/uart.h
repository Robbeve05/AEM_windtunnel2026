/*
 * Host-only UART interface for the unmodified V20 driver.
 * Author: R.E. van Eekelen
 */
#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#define UART_PARITY_EVEN 2
#define UART_STOP_BITS_1 1
#define UART_DATA_8_BITS 8
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_APB 1
#define UART_PIN_NO_CHANGE -1
#define UART_MODE_RS485_HALF_DUPLEX 1
typedef struct {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
} uart_config_t;
esp_err_t uart_param_config(int port, const uart_config_t *config);
esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts);
esp_err_t uart_driver_install(int port, int rx_size, int tx_size, int queue_size,
                             void *queue, int flags);
esp_err_t uart_set_mode(int port, int mode);
esp_err_t uart_flush_input(int port);
int uart_write_bytes(int port, const void *data, size_t length);
int uart_read_bytes(int port, void *data, unsigned length, TickType_t wait);
