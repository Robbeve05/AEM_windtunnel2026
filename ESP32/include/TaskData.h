#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
typedef struct { float value; TickType_t tick; bool valid; } Sample;
typedef struct { float output; TickType_t tick; } MotorRequest;
typedef struct { bool ready; bool ok; TickType_t tick; } MotorStatus;
typedef struct {
    float setpoint, measurement, output;
    bool running, sensor_ok, motor_ok;
    const char *reason;
} Telemetry;
extern QueueHandle_t commands, sensors, motor_requests, motor_status, telemetry;
extern EventGroupHandle_t events;
#define STOP_REQUEST BIT0
void ControlTask(void *argument);
void SensorTask(void *argument);
void ActuatorTask(void *argument);
void PiCommunicationTask(void *argument);
