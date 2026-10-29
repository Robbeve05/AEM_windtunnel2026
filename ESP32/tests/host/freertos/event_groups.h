/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include "FreeRTOS.h"
typedef uint32_t EventBits_t;
typedef EventBits_t *EventGroupHandle_t;
EventBits_t xEventGroupWaitBits(EventGroupHandle_t,EventBits_t,int,int,TickType_t);
EventBits_t xEventGroupSetBits(EventGroupHandle_t,EventBits_t);

EventGroupHandle_t xEventGroupCreate(void);
