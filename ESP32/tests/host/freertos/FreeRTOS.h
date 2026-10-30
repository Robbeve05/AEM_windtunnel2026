/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
typedef uint32_t TickType_t;
typedef unsigned UBaseType_t;
typedef int BaseType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#ifdef HOST_OUTPUT_LOCKS
void Host_EnterCritical(portMUX_TYPE *lock);
void Host_ExitCritical(portMUX_TYPE *lock);
#define portENTER_CRITICAL(x) Host_EnterCritical(x)
#define portEXIT_CRITICAL(x) Host_ExitCritical(x)
#else
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#endif
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define BIT0 1u
#define IRAM_ATTR

#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define portTICK_PERIOD_MS 1u
