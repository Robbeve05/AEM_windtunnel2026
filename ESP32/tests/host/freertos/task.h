/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t*);
uint32_t ulTaskNotifyTake(int,TickType_t);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
void vTaskDelay(TickType_t);

void vTaskDelayUntil(TickType_t*,TickType_t);

BaseType_t xTaskCreatePinnedToCore(void (*task)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*,int);
