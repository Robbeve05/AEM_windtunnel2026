/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef void *gptimer_handle_t;
typedef struct {int unused;} gptimer_alarm_event_data_t;
typedef struct { int clk_src,direction,resolution_hz; } gptimer_config_t;
typedef struct {uint64_t alarm_count,reload_count; struct {bool auto_reload_on_alarm;} flags;} gptimer_alarm_config_t;
typedef struct {bool (*on_alarm)(gptimer_handle_t,const gptimer_alarm_event_data_t*,void*);} gptimer_event_callbacks_t;
#define GPTIMER_CLK_SRC_DEFAULT 0
#define GPTIMER_COUNT_UP 0
int gptimer_new_timer(const gptimer_config_t*,gptimer_handle_t*);
int gptimer_set_alarm_action(gptimer_handle_t,const gptimer_alarm_config_t*);
int gptimer_register_event_callbacks(gptimer_handle_t,const gptimer_event_callbacks_t*,void*);
int gptimer_enable(gptimer_handle_t);
int gptimer_start(gptimer_handle_t);
