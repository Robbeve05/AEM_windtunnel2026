/* Author: R.E. van Eekelen. Host-only test interface. */

#pragma once
#include "FreeRTOS.h"
typedef struct { unsigned size,count; unsigned char data[4096]; } Queue;
typedef Queue *QueueHandle_t;
int xQueuePeek(QueueHandle_t,void*,TickType_t);
int xQueueOverwrite(QueueHandle_t,const void*);
int xQueueReceive(QueueHandle_t,void*,TickType_t);

QueueHandle_t xQueueCreate(unsigned,unsigned);
