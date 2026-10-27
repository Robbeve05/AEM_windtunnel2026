/*
 * ControlTask.c
 * Core 1: 200 Hz (5 ms) state machine, PI controller and non-blocking V20 service.
 *
 * Author: R.E. van Eekelen
 *
 * Operational control only. Independent hardware must implement E-stop.
 */
#include "TaskData.h"
#include "AppConfig.h"
#include "PiProtocol.h"
#include "Regelaar.h"
#include "SetpointCurve.h"
#include "V20Modbus.h"
#include "SafetyIO.h"
#include "ControlTiming.h"
#include "PressureSensors.h"

#include <stdint.h>
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

_Static_assert(CONTROL_PERIOD_US == (1000000u / CONTROL_FREQUENCY_HZ),
               "GPTimer and PI sample periods must be equal");
_Static_assert(CONTROL_FREQUENCY_HZ == 200u,
               "The approved wind tunnel controller runs at 200 Hz");

static bool IRAM_ATTR ControlClockInterrupt(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *event,
    void *argument)
{
    (void)timer;
    (void)event;
    (void)argument;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(control_task_handle, &wake);
    return wake == pdTRUE;
}

static void StartControlClock(void)
{
    gptimer_handle_t timer = NULL;
    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000
    };
    const gptimer_alarm_config_t alarm = {
        .alarm_count = CONTROL_PERIOD_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true
    };
    const gptimer_event_callbacks_t callbacks = {
        .on_alarm = ControlClockInterrupt
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&config, &timer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));
}

/* Overwrite one-element queues; there is never a backlog of motor commands. */
static void PublishControl(Telemetry *telemetry_state,
                           const StateMachine *machine,
                           TickType_t now)
{
    telemetry_state->state = machine->state;
    telemetry_state->fault = machine->fault;
    telemetry_state->last_fault = machine->last_fault;
    telemetry_state->running = machine->state == STATE_RUNNING;
    telemetry_state->reset_required =
        machine->state == STATE_FAULT || machine->state == STATE_WAIT;
    telemetry_state->reason = machine->fault == FAULT_NONE
        ? StateMachine_Name(machine->state)
        : FaultReason_Name(machine->fault);
    telemetry_state->transitions = machine->transition_count;
    if (!telemetry_state->running) telemetry_state->output = 0.0f;

    const MotorRequest command = {
        .output = telemetry_state->output,
        .tick = now,
        .run = telemetry_state->running,
        .permissive = machine->permissive,
        .coast = machine->state == STATE_FAULT,
        .epoch = machine->epoch
    };
    xQueueOverwrite(motor_requests, &command);
    xQueueOverwrite(telemetry, telemetry_state);
}

void ControlTask(void *argument)
{
    (void)argument;
    control_task_handle = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    Regelaar controller;
    ESP_ERROR_CHECK(Regelaar_INIT(&controller, CONTROL_CLOCK_PERIOD_S)
        ? ESP_OK : ESP_FAIL);

    SetpointCurve curve = {0};
    bool curve_running = false;
    StateMachine machine;
    StateMachine_Init(&machine, (uint32_t)(esp_timer_get_time() / 1000));

    const bool pitot_initialized = SensorLib_Init();
    const bool v20_initialized = V20Modbus_Init();
    if (!v20_initialized)
        StateMachine_Fault(&machine, FAULT_MODBUS,
                           (uint32_t)(esp_timer_get_time() / 1000));

    Telemetry status = {0};
    ControlTiming timing_stats = {0};
    FaultReason_t latched_timing_fault = FAULT_NONE;
    uint32_t previous_start_sequence = 0;
    uint32_t previous_reset_sequence = 0;
    uint32_t cycles = 0;
    int64_t window_start_us = esp_timer_get_time();

    PublishControl(&status, &machine, xTaskGetTickCount());
    StartControlClock();

    for (;;)
    {
        const uint32_t notifications = ulTaskNotifyTake(
            pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
        const int64_t started_us = esp_timer_get_time();
        const uint32_t now_ms = (uint32_t)(started_us / 1000);
        const TickType_t now_tick = xTaskGetTickCount();
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        const FaultReason_t clock_fault = ControlTiming_Tick(
            &timing_stats, (uint64_t)started_us, notifications);
        if (clock_fault != FAULT_NONE) latched_timing_fault = clock_fault;

        status.max_jitter_us = timing_stats.max_jitter_us;
        status.missed_deadlines = timing_stats.missed_deadlines;
        cycles++;
        if (started_us - window_start_us >= 1000000) {
            status.control_hz = (float)cycles * 1000000.0f /
                                (float)(started_us - window_start_us);
            cycles = 0;
            window_start_us = started_us;
        }

        /* Core 1 exclusively samples the pitot once per control tick.
         * Publish a new 10-sample average approximately every 50 ms.
         * Keep using the last published valid sample between updates.
         * All other pressure taps remain on Core 0. */
        Sample updated_pitot = {0};
        if (pitot_initialized && SensorLib_PitotStep(&updated_pitot))
            xQueueOverwrite(sensors, &updated_pitot);
        Sample sample = {0};
        SafetySnapshot safety = {0};
        PiHeartbeat heartbeat = {0};
        MotorStatus motor = {0};
        (void)xQueuePeek(sensors, &sample, 0);
        (void)xQueuePeek(safety_status, &safety, 0);
        (void)xQueuePeek(pi_heartbeat, &heartbeat, 0);
        V20Modbus_GetStatus(&motor);

        bool start = safety.start_sequence != previous_start_sequence;
        bool reset = safety.reset_sequence != previous_reset_sequence;
        previous_start_sequence = safety.start_sequence;
        previous_reset_sequence = safety.reset_sequence;

        /* An old START edge must never be replayed when READY is entered. */
        start = start && machine.state == STATE_READY &&
            (int32_t)(safety.start_ms - machine.entered_ms) > 0;

        bool stop = SafetyIO_Stop() ||
            (xEventGroupWaitBits(events, STOP_REQUEST, pdTRUE, pdFALSE, 0) &
             STOP_REQUEST) != 0;

        PiCommand command;
        for (unsigned i = 0;
             i < 16 && xQueueReceive(commands, &command, 0) == pdTRUE;
             ++i) {
            switch (command.type) {
            case CMD_SETPOINT:
                status.setpoint = command.values[0];
                break;
            case CMD_PID:
                if (machine.state != STATE_RUNNING && machine.state != STATE_STOP)
                    (void)Regelaar_Instellen(&controller,
                        command.values[0], command.values[1], command.values[2],
                        CONTROL_CLOCK_PERIOD_S, 0.0f, MAX_OUTPUT, 0.05f);
                break;
            case CMD_STOP:
                stop = true;
                break;
#if ALLOW_REMOTE_START
            case CMD_START:
                if (machine.state == STATE_READY) start = true;
                break;
#endif
            case CMD_FILTER:
            case CMD_PING:
            default:
                break;
            }
        }

        /* Physical reset clears a timing latch only when the input is healthy. */
        if (reset && clock_fault == FAULT_NONE && safety.valid &&
            !safety.estop_pressed &&
            (machine.state == STATE_WAIT || machine.state == STATE_FAULT))
            latched_timing_fault = FAULT_NONE;

        if (machine.reset_pending && !safety.estop_pressed && safety.relay_ok)
            SafetyIO_Acknowledge();

        status.measurement = sample.value;
        status.sensor_ok = sample.valid &&
            (TickType_t)(now_tick - sample.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
        status.motor_ok = motor.ready && motor.ok;
        status.pi_ok = !PI_HEARTBEAT_REQUIRED ||
            (heartbeat.connected &&
             (TickType_t)(now_tick - heartbeat.tick) <
                pdMS_TO_TICKS(COMMAND_TIMEOUT_MS));
        status.safety = safety;

        /* This becomes true only after a fresh, acknowledged OFF1 and status
         * frame from the V20, not merely because an old speed was zero. */
        const bool stop_confirmed = machine.state == STATE_STOP &&
            V20Modbus_StopConfirmed((int64_t)machine.entered_ms * 1000LL,
                                    STOP_CONFIRM_HZ);

        const MachineInputs inputs = {
            .now_ms = now_ms,
            .safety = safety,
            .sensor_ok = status.sensor_ok,
            .drive_ready = motor.ready,
            .drive_ok = motor.ok,
            .drive_fault = (motor.status & 0x0008u) != 0,
            .pi_ok = status.pi_ok,
            .stop = stop,
            .start = start,
            .reset = reset,
            .urgent_fault = SafetyIO_Fault(),
            .timing_fault = latched_timing_fault,
            .stop_confirmed = stop_confirmed
        };

        const SystemState_t previous_state = machine.state;
        StateMachine_Step(&machine, &inputs);

        if (machine.recovery_requested)
            V20Modbus_RequestRecheck();

        /* A new physical START is the ONLY place that may arm the VFD. */
        if (previous_state == STATE_READY && machine.state == STATE_RUNNING) {
            if (!V20Modbus_Arm())
                StateMachine_Fault(&machine, FAULT_MODBUS, now_ms);
        }

        /* Disarm immediately on STOP or FAULT; the separate operational K2
         * permissive is kept enabled through a healthy normal OFF1 ramp. */
        if ((previous_state == STATE_RUNNING && machine.state != STATE_RUNNING) ||
            (previous_state != STATE_FAULT && machine.state == STATE_FAULT))
            V20Modbus_Disarm();

        status.output = 0.0f;
        if (machine.state == STATE_RUNNING && clock_fault == FAULT_NONE) {
            if (!curve_running)
                curve_running = SetpointCurve_Init(&curve, sample.value,
                    SETPOINT_TRANSITION_TIME_S, SETPOINT_MAX_ACCELERATION_MPS2,
                    SETPOINT_MAX_JERK_MPS3);
            if (!curve_running ||
                !SetpointCurve_Step(&curve, status.setpoint,
                                    CONTROL_CLOCK_PERIOD_S,
                                    &status.control_setpoint) ||
                !PIDregelaar(&controller,
                             status.control_setpoint - sample.value,
                             sample.value, &status.output)) {
                StateMachine_Fault(&machine, FAULT_INTERNAL, now_ms);
                V20Modbus_Disarm();
            }
        }
        if (machine.state != STATE_RUNNING) {
            curve_running = false;
            status.control_setpoint = sample.value;
            status.output = 0.0f;
            removeRegelaarHistory(&controller);
        }

        if (machine.state == STATE_RUNNING &&
            (motor.status & 0x0040u) != 0 &&
            now_ms - machine.entered_ms > RESET_FEEDBACK_WINDOW_MS) {
            StateMachine_Fault(&machine, FAULT_VFD, now_ms);
            V20Modbus_Disarm();
        }

        /* Immediately publish the state so SafetyTask can disable K2. */
        PublishControl(&status, &machine, now_tick);
        MotorRequest request = {0};
        (void)xQueuePeek(motor_requests, &request, 0);
        const bool safety_ok = safety.valid && safety.commissioned &&
            safety.relay_ok && !safety.estop_pressed && safety.outputs_ok &&
            SafetyIO_Fault() == FAULT_NONE;

        const int64_t service_started_us = esp_timer_get_time();
        if (v20_initialized)
            V20Modbus_Service(&request, safety_ok);
        status.service_us = (uint32_t)(esp_timer_get_time() - service_started_us);
        if (status.service_us > status.service_max_us)
            status.service_max_us = status.service_us;

        V20Modbus_GetStatus(&motor);
        xQueueOverwrite(motor_status, &motor);
        status.stack_free = uxTaskGetStackHighWaterMark(NULL);
        status.execution_us = (uint32_t)(esp_timer_get_time() - started_us);
        if (!ControlTiming_Execution(&timing_stats, status.execution_us,
                                     status.service_us)) {
            latched_timing_fault = FAULT_CONTROL_OVERRUN;
            StateMachine_Fault(&machine, latched_timing_fault, now_ms);
            V20Modbus_Disarm();
        }
        if (!v20_initialized ||
            (!motor.ok && (machine.state == STATE_RUNNING ||
                           machine.state == STATE_READY ||
                           machine.state == STATE_STOP))) {
            StateMachine_Fault(&machine, FAULT_MODBUS, now_ms);
            V20Modbus_Disarm();
        }
        status.overruns = timing_stats.overruns;
        PublishControl(&status, &machine, now_tick);
    }
}
