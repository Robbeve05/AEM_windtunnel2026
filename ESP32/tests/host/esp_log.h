/* Author: R.E. van Eekelen. Host-only logging adapter. */
#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, ...) ((void)fprintf(stderr, "%s: ", tag), \
                           (void)fprintf(stderr, __VA_ARGS__), (void)fputc('\n', stderr))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))

#define ESP_LOGD(tag, ...) ((void)(tag))
