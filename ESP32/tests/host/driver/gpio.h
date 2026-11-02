/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include "esp_err.h"
static inline esp_err_t gpio_install_isr_service(int flags) {(void)flags;return ESP_OK;}

#include <stdint.h>
typedef struct { uint64_t pin_bit_mask; int mode, intr_type; } gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_ANYEDGE 3
int gpio_get_level(int pin);
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_intr_type(int pin, int type);
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg);
