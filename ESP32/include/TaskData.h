#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
typedef struct { float value, pressure_pa, temperature_c; float taps[16]; uint16_t taps_valid; uint32_t errors, sequence; TickType_t tick; bool valid; } Sample;
typedef struct { float output; TickType_t tick; bool run; } MotorRequest;
typedef struct { bool ready; bool ok; float actual_hz; uint16_t status; uint32_t errors, timeouts; TickType_t tick; } MotorStatus;
typedef struct {
    float setpoint, measurement, output, control_setpoint;
    bool running, sensor_ok, motor_ok;
    const char *reason;
    float control_hz; uint32_t execution_us, max_jitter_us, missed_deadlines, overruns, stack_free;
} Telemetry;
extern QueueHandle_t commands, sensors, motor_requests, motor_status, telemetry;
extern EventGroupHandle_t events;
extern TaskHandle_t control_task_handle;
#define STOP_REQUEST BIT0
#define INTERLOCK_OPEN BIT1
#define RESET_TIMING_FAULT BIT2
void SafetyTask(void *argument);
void ControlTask(void *argument);
void SensorTask(void *argument);
void ActuatorTask(void *argument);
void PiCommunicationTask(void *argument);
