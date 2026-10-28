/* Author: R.E. van Eekelen */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "StateMachine.h"
typedef struct { float value,pressure_pa,temperature_c,taps[16]; uint16_t taps_valid; uint32_t errors,sequence; TickType_t tick; bool valid; } Sample;
typedef struct { float output; TickType_t tick; bool run,permissive,coast; uint32_t epoch; } MotorRequest;
typedef struct { bool ready,ok; float actual_hz; uint16_t status; uint32_t errors,timeouts; TickType_t tick; } MotorStatus;
typedef struct { bool connected; TickType_t tick; } PiHeartbeat;
typedef struct {
    float setpoint,measurement,output,control_setpoint,manual_hz_requested,manual_hz_applied;
    bool manual_mode;
    bool running,sensor_ok,motor_ok,pi_ok,reset_required;
    SystemState_t state; FaultReason_t fault,last_fault; SafetySnapshot safety;
    const char *reason;
    float control_hz,modbus_hz;
    uint32_t execution_us,max_jitter_us,missed_deadlines,overruns,stack_free;
    uint32_t crc_errors,exceptions,late_replies,exception_code;
    uint32_t service_us,service_max_us,rtt_median_us,rtt_p95_us,rtt_max_us,request_id,transitions;
} Telemetry;
extern QueueHandle_t commands,sensors,motor_requests,motor_status,telemetry,safety_status,pi_heartbeat,tap_samples;
extern EventGroupHandle_t events;
extern TaskHandle_t control_task_handle, sensor_task_handle;
#define STOP_REQUEST BIT0
void SafetyTask(void *argument);
void ControlTask(void *argument);
void SensorTask(void *argument);
void PiCommunicationTask(void *argument);
