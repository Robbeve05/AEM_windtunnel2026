/* Author: R.E. van Eekelen. Host-only logging adapter. */
#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, message) ((void)fprintf(stderr, "%s: %s\n", tag, message))
