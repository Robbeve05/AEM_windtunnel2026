/* Author: R.E. van Eekelen */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "StateMachine.h"
#include "PiProtocol.h"
typedef struct { float value,pressure_pa,temperature_c,taps[16]; uint16_t taps_valid; TickType_t tap_ticks[16]; uint32_t errors,sequence; TickType_t tick; bool valid; } Sample;
typedef struct { float output; TickType_t tick; bool run,permissive,coast; uint32_t epoch; } MotorRequest;
typedef struct {
    bool ready, ok, initialized, response_received, status_healthy, communication_ok;
    float actual_hz;
    uint16_t status;
    uint32_t errors, timeouts;
    TickType_t tick;
} MotorStatus;
typedef struct { bool connected; TickType_t tick; } PiHeartbeat;
typedef struct {
    uint32_t blocking_reasons;
    uint32_t modbus_errors, modbus_timeouts;
    uint16_t v20_status_word;
    bool estop_released, stop_ok, reset_detected, reset_accepted, reset_pending;
    bool safety_inputs_valid, safety_inputs_fresh, safety_commissioned, safety_relay_ok;
    bool safety_outputs_healthy, digital_outputs_initialized, digital_outputs_healthy;
    bool modbus_initialized, modbus_response_received, v20_status_healthy, v20_ready;
    bool drive_communication_ok, v20_run_commissioned;
    bool pitot_communication_ok, sensor_data_valid, sensor_data_fresh, sensor_ok;
    bool ethernet_link, pi_heartbeat_received, pi_heartbeat_fresh, pi_ok;
} ReadyDiagnostics;
typedef struct {
    float setpoint,measurement,output,control_setpoint;
    OperatingMode mode;
    float manual_hz,applied_hz;
    bool running,sensor_ok,motor_ok,pi_ok,reset_required;
    SystemState_t state; FaultReason_t fault,last_fault; SafetySnapshot safety;
    const char *reason;
    float control_hz,modbus_hz;
    uint32_t execution_us,max_jitter_us,missed_deadlines,overruns,stack_free;
    uint32_t crc_errors,exceptions,late_replies,exception_code;
    uint32_t service_us,service_max_us,rtt_median_us,rtt_p95_us,rtt_max_us,request_id,transitions;
    uint32_t control_cycle_count;
    bool control_ready,control_clock_started,modbus_initialized;
    ReadyDiagnostics ready_diag;
} Telemetry;
extern QueueHandle_t commands,sensors,motor_requests,motor_status,telemetry,safety_status,pi_heartbeat;
extern EventGroupHandle_t events;
extern TaskHandle_t control_task_handle;
extern TaskHandle_t sensor_task_handle;
#define STOP_REQUEST BIT0
void InputHandlerTask(void *argument);
void ControlTask(void *argument);
void SensorTask(void *argument);
void PiCommunicationTask(void *argument);
