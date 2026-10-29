/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include "esp_err.h"
static inline esp_err_t gpio_install_isr_service(int flags) {(void)flags;return ESP_OK;}
