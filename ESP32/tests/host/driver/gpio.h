/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include "esp_err.h"
static inline esp_err_t gpio_install_isr_service(int flags) {(void)flags;return ESP_OK;}

#ifdef HOST_GPIO_EVENTS
#include <stdint.h>
typedef struct { uint64_t pin_bit_mask; int mode, intr_type; } gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_ANYEDGE 3
#define portENTER_CRITICAL_ISR(x) portENTER_CRITICAL(x)
#define portEXIT_CRITICAL_ISR(x) portEXIT_CRITICAL(x)
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_intr_type(int gpio, int type);
esp_err_t gpio_isr_handler_add(int gpio, void (*handler)(void *), void *arg);
int gpio_get_level(int gpio);
#endif
