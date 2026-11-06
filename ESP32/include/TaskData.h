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
    bool drive_fault_free, operational_ready, run_authorized;
    bool ready_to_switch_on, ready_to_operate, operation_enabled, off2_active, off3_active;
    bool switch_on_inhibited, control_word_requested_valid, control_word_acknowledged_valid;
    bool diagnostics_complete, status_valid, reference_valid, reference_config_valid;
    bool watchdog_valid, watchdog_config_known, watchdog_config_valid;
    float actual_hz;
    uint16_t status, control_word_requested, control_word_acknowledged;
    uint16_t reference_value, watchdog_register_value;
    uint16_t watchdog_parameter_expected_value;
    uint16_t last_error_register, last_error_expected, last_error_actual;
    uint32_t errors, timeouts, last_success_age_ms;
    uint32_t exception_code, last_error_count;
    uint8_t last_error, last_operation;
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
    bool v20_ready_to_switch_on, v20_ready_to_operate, v20_operation_enabled;
    bool v20_off2_active, v20_off3_active, v20_switch_on_inhibited;
    bool v20_control_word_requested_valid, v20_control_word_acknowledged_valid;
    bool do1_requested, do1_acknowledged, do1_ack_valid, relay_feedback_ok;
    bool drive_fault_free, operational_ready, run_authorized, diagnostics_complete;
    bool reference_valid, reference_config_valid, watchdog_valid, watchdog_config_known;
    bool watchdog_config_valid;
    uint16_t v20_control_word_requested, v20_control_word_acknowledged;
    uint16_t reference_value, watchdog_register_value, watchdog_parameter_expected_value;
    uint8_t modbus_last_error, modbus_last_operation;
    uint16_t modbus_last_error_register, modbus_last_error_expected, modbus_last_error_actual;
    uint32_t modbus_exception_code, modbus_last_success_age_ms;
    bool pitot_communication_ok, sensor_data_valid, sensor_data_fresh, sensor_ok;
    bool ethernet_link, pi_heartbeat_received, pi_heartbeat_fresh, pi_ok;
} ReadyDiagnostics;
typedef struct {
    float setpoint,measurement,output,control_setpoint;
    OperatingMode mode;
    float manual_hz,applied_hz;
    bool running,sensor_ok,motor_ok,pi_ok,reset_required;
    SystemState_t state,fault_from; FaultReason_t fault,last_fault; SafetySnapshot safety;
    const char *reason;
    float control_hz,modbus_hz;
    uint32_t execution_us,max_jitter_us,missed_deadlines,overruns,stack_free;
    uint32_t timing_fault_execution_us,timing_fault_service_us,timing_fault_missed_ticks;
    FaultReason_t timing_fault_reason;
    uint32_t crc_errors,exceptions,late_replies,exception_code;
    uint32_t service_us,service_max_us,rtt_median_us,rtt_p95_us,rtt_max_us,request_id,transitions;
    uint32_t control_cycle_count;
    bool control_ready,control_clock_started,modbus_initialized,init_deadline_exceeded;
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
