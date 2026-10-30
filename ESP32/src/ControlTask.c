///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * ControlTask.c
 *
 * Wind tunnel system control and state machine coordination.
 *
 * Author: R.E. van Eekelen
 */
///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdint.h>
#include <math.h>
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "ApplicationTasks.h"
#include "TaskData.h"
#include "AppConfig.h"
#include "PiProtocol.h"
#include "Regelaar.h"
#include "SetpointCurve.h"
#include "V20Modbus.h"
#include "SafetyIO.h"

///////////////////////////////////////////////////////////////////////////////
// Local definitions

_Static_assert(CONTROL_PERIOD_US == (1000000u / CONTROL_FREQUENCY_HZ),
               "GPTimer and PI sample periods must be equal");
_Static_assert(SENSOR_PHASE_US * 2 == CONTROL_PERIOD_US, "Two equal clock phases required");
_Static_assert(CONTROL_FREQUENCY_HZ == 200u, "The approved wind tunnel controller runs at 200 Hz");

///////////////////////////////////////////////////////////////////////////////
// File-local data structures

typedef struct
{
    uint64_t previous_us;
    uint32_t max_jitter_us, missed_deadlines, overruns;
} ControlTiming;

typedef struct
{
    Regelaar controller;
    SetpointCurve curve;
    SetpointCurve frequency_curve;
    bool frequency_running;
    StateMachine machine;
    Telemetry status;
    ControlTiming timing_stats;
    Sample sample;
    SafetySnapshot safety;
    PiHeartbeat heartbeat;
    MotorStatus motor;
    FaultReason_t latched_timing_fault;
    uint32_t previous_start_sequence;
    uint32_t previous_reset_sequence;
    bool curve_running;
    bool v20_initialized;
    uint32_t cycles;
    int64_t window_start_us;

    /* Per-cycle values shared by the ordered processing stages. */
    int64_t started_us;
    uint32_t now_ms;
    TickType_t now_tick;
    FaultReason_t clock_fault;
    MachineInputs inputs;
    bool start, stop, reset;
    bool tasks_ready;
} ControlContext;

///////////////////////////////////////////////////////////////////////////////
// File-local variables

/* Static storage preserves the existing task stack budget. */
static ControlContext control;

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static void Control_Init(ControlContext *ctx);
static void Control_WaitForTasks(ControlContext *ctx);
static void Control_HoldInitializationFault(ControlContext *ctx);
static EventBits_t Control_ReadButtonEvents(void);
static void Control_ReadInputs(ControlContext *ctx);
static void Control_ProcessCommands(ControlContext *ctx, EventBits_t buttonBits);
static void Control_CheckSafety(ControlContext *ctx);
static void Control_UpdateStateMachine(ControlContext *ctx);
static void Control_UpdateController(ControlContext *ctx);
static void Control_UpdateMotor(ControlContext *ctx);
static void Control_PublishStatus(ControlContext *ctx);
static void Control_UpdateDiagnostics(ControlContext *ctx);
static void Control_BeginCycle(ControlContext *ctx, uint32_t notifications);
static void StartControlClock(void);
static bool ControlClockInterrupt(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event,
                                  void *argument);

///////////////////////////////////////////////////////////////////////////////
// Timer interrupt handling

/*
 * Notifies ControlTask once per GPTimer alarm; no EventGroup drives the clock.
 */
static bool IRAM_ATTR ControlClockInterrupt(gptimer_handle_t timer,
                                            const gptimer_alarm_event_data_t *event, void *argument)
{
    (void)timer;
    (void)event;
    (void)argument;
    BaseType_t wake = pdFALSE;
    static bool sensor_phase = true;
    vTaskNotifyGiveFromISR(sensor_phase ? sensor_task_handle : control_task_handle, &wake);
    sensor_phase = !sensor_phase;
    return wake == pdTRUE;
}

/*
 * Starts the unchanged 5 ms GPTimer only after startup synchronization.
 */
static void StartControlClock(void)
{
    gptimer_handle_t timer = NULL;
    const gptimer_config_t config = {.clk_src = GPTIMER_CLK_SRC_DEFAULT,
                                     .direction = GPTIMER_COUNT_UP,
                                     .resolution_hz = 1000000};
    const gptimer_alarm_config_t alarm = {
        .alarm_count = SENSOR_PHASE_US, .reload_count = 0, .flags.auto_reload_on_alarm = true};
    const gptimer_event_callbacks_t callbacks = {.on_alarm = ControlClockInterrupt};

    ESP_ERROR_CHECK(gptimer_new_timer(&config, &timer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));
}

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// static void Control_Init(ControlContext *ctx)

/*
 * Initializes control, diagnostics and disabled motor commands before startup synchronization.
 */
static void Control_Init(ControlContext *ctx)
{
    *ctx = (ControlContext){0};
    control_task_handle = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(Regelaar_INIT(&ctx->controller, CONTROL_CLOCK_PERIOD_S) ? ESP_OK : ESP_FAIL);
    ctx->now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ctx->now_tick = xTaskGetTickCount();
    StateMachine_Init(&ctx->machine, ctx->now_ms);
    ctx->v20_initialized = V20Modbus_Init();
    if (!ctx->v20_initialized)
        StateMachine_Fault(&ctx->machine, FAULT_MODBUS, ctx->now_ms);
    V20Modbus_Disarm();
    ctx->window_start_us = esp_timer_get_time();
    Control_PublishStatus(ctx);
}

///////////////////////////////////////////////////////////////////////////////
// static void Control_WaitForTasks(ControlContext *ctx)

/*
 * Waits within the existing initialization deadline; ready bits remain set.
 * Readiness means task initialization completed, not that external hardware is healthy.
 */
static void Control_WaitForTasks(ControlContext *ctx)
{
    while ((uint32_t)(esp_timer_get_time() / 1000) - ctx->machine.boot_ms < INIT_TIMEOUT_MS)
    {
        uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000) - ctx->machine.boot_ms;
        uint32_t remaining = elapsed < INIT_TIMEOUT_MS ? INIT_TIMEOUT_MS - elapsed : 0;
        uint32_t wait_ms =
            remaining < CONTROL_CLOCK_TIMEOUT_MS ? remaining : CONTROL_CLOCK_TIMEOUT_MS;
        EventBits_t ready = xEventGroupWaitBits(handle_ThreadEventGroup, EVT_ALL_TASKS_READY,
                                                pdFALSE, pdTRUE, pdMS_TO_TICKS(wait_ms));
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        if ((ready & EVT_ALL_TASKS_READY) == EVT_ALL_TASKS_READY)
        {
            ctx->tasks_ready = true;
            return;
        }
    }
    ctx->now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ctx->now_tick = xTaskGetTickCount();
    StateMachine_Fault(&ctx->machine, FAULT_INTERNAL, ctx->now_ms);
    V20Modbus_Disarm();
    Control_PublishStatus(ctx);
}

///////////////////////////////////////////////////////////////////////////////
// static void Control_HoldInitializationFault(ControlContext *ctx)

/*
 * Keeps failed startup disabled until reboot, without starting the control timer.
 * Continues publishing faults and servicing disabled V20 requests for diagnostics.
 */
static void Control_HoldInitializationFault(ControlContext *ctx)
{
    for (;;)
    {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        ctx->now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ctx->now_tick = xTaskGetTickCount();
        (void)Control_ReadButtonEvents();
        Control_ReadInputs(ctx);
        StateMachine_Fault(&ctx->machine, SafetyIO_Fault(), ctx->now_ms);
        V20Modbus_Disarm();
        Control_PublishStatus(ctx);
        Control_UpdateMotor(ctx);
        Control_PublishStatus(ctx);
        vTaskDelay(pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
    }
}

///////////////////////////////////////////////////////////////////////////////
// Input processing

///////////////////////////////////////////////////////////////////////////////
// static EventBits_t Control_ReadButtonEvents(void)

/*
 * Consumes one-shot physical button notifications without waiting or reading GPIOs.
 */
static EventBits_t Control_ReadButtonEvents(void)
{
    return xEventGroupWaitBits(handle_ButtonEventGroup, EVT_ALL_BUTTONS, pdTRUE, pdFALSE, 0) &
           EVT_ALL_BUTTONS;
}

///////////////////////////////////////////////////////////////////////////////
// static void Control_ReadInputs(ControlContext *ctx)

/*
 * Reads and validates latest queue values every cycle, even without advisory update bits.
 */
static void Control_ReadInputs(ControlContext *ctx)
{
    (void)xEventGroupWaitBits(handle_SystemEventGroup, EVT_NEW_COMMAND | EVT_SENSOR_UPDATED, pdTRUE,
                              pdFALSE, 0);
    ctx->sample = (Sample){0};
    ctx->safety = (SafetySnapshot){0};
    ctx->heartbeat = (PiHeartbeat){0};
    ctx->motor = (MotorStatus){0};
    (void)xQueuePeek(sensors, &ctx->sample, 0);
    (void)xQueuePeek(safety_status, &ctx->safety, 0);
    (void)xQueuePeek(pi_heartbeat, &ctx->heartbeat, 0);
    V20Modbus_GetStatus(&ctx->motor);

    ctx->status.measurement = ctx->sample.value;
    ctx->status.sensor_ok = ctx->sample.valid && (TickType_t)(ctx->now_tick - ctx->sample.tick) <
                                                     pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
    ctx->status.motor_ok = ctx->motor.ready && ctx->motor.ok;
    ctx->status.pi_ok =
        !PI_HEARTBEAT_REQUIRED ||
        (ctx->heartbeat.connected &&
         (TickType_t)(ctx->now_tick - ctx->heartbeat.tick) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS));
    ctx->status.safety = ctx->safety;
}

///////////////////////////////////////////////////////////////////////////////
// Command processing

///////////////////////////////////////////////////////////////////////////////
// static void Control_ProcessCommands(ControlContext *ctx, EventBits_t buttonBits)

/*
 * Processes physical actions and at most sixteen queued commands, retaining all START interlocks.
 */
static void Control_ProcessCommands(ControlContext *ctx, EventBits_t buttonBits)
{
    const bool fresh_start = ctx->safety.start_sequence != ctx->previous_start_sequence;
    const bool fresh_reset = ctx->safety.reset_sequence != ctx->previous_reset_sequence;
    ctx->previous_start_sequence = ctx->safety.start_sequence;
    ctx->previous_reset_sequence = ctx->safety.reset_sequence;

    /* Sequences remain authoritative across queue/event publication races.
     * A late or coalesced notification cannot repeat a consumed action. */
    ctx->start = ((buttonBits & EVT_START_BUTTON) != 0 || fresh_start) && fresh_start;
    ctx->reset = ((buttonBits & EVT_RESET_BUTTON) != 0 || fresh_reset) && fresh_reset;

    /* An old START edge must never be replayed when READY is entered. */
    ctx->start = ctx->start && ctx->machine.state == STATE_READY &&
                 (int32_t)(ctx->safety.start_ms - ctx->machine.entered_ms) > 0;

    ctx->stop = SafetyIO_Stop() || (buttonBits & EVT_STOP_BUTTON) != 0 ||
                (xEventGroupWaitBits(events, STOP_REQUEST, pdTRUE, pdFALSE, 0) & STOP_REQUEST) != 0;

    PiCommand command;
    for (unsigned i = 0; i < 16 && xQueueReceive(commands, &command, 0) == pdTRUE; ++i)
    {
        switch (command.type)
        {
        case CMD_MODE:
            if (ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STOP) {
                ctx->status.mode = (OperatingMode)(int)command.values[0];
                ctx->status.manual_hz = ctx->status.applied_hz = ctx->status.output = 0;
                ctx->frequency_running = ctx->curve_running = false;
                removeRegelaarHistory(&ctx->controller);
                ctx->start = false;
            }
            break;
        case CMD_FREQ:
            if (ctx->status.mode == MODE_MANUAL && isfinite(command.values[0]) &&
                command.values[0] >= 0 && command.values[0] <= MANUAL_MAX_HZ)
                ctx->status.manual_hz = command.values[0];
            break;
        case CMD_SETPOINT:
            ctx->status.setpoint = command.values[0];
            break;
        case CMD_PID:
            if (ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STOP)
                (void)Regelaar_Instellen(&ctx->controller, command.values[0], command.values[1],
                                         command.values[2], CONTROL_CLOCK_PERIOD_S, 0.0f,
                                         MAX_OUTPUT, 0.05f);
            break;
        case CMD_STOP:
            ctx->stop = true;
            break;

        case CMD_FILTER:
        case CMD_PING:
        default:
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// Safety and state machine

///////////////////////////////////////////////////////////////////////////////
// static void Control_CheckSafety(ControlContext *ctx)

/*
 * Builds state-machine inputs and preserves physical reset acknowledgement and fault priorities.
 */
static void Control_CheckSafety(ControlContext *ctx)
{
    /* Physical reset clears a timing latch only when the input is healthy. */
    if (ctx->reset && ctx->clock_fault == FAULT_NONE && ctx->safety.valid &&
        !ctx->safety.estop_pressed &&
        (ctx->machine.state == STATE_WAIT || ctx->machine.state == STATE_FAULT))
        ctx->latched_timing_fault = FAULT_NONE;

    if (ctx->machine.reset_pending && !ctx->safety.estop_pressed && ctx->safety.relay_ok)
        SafetyIO_Acknowledge();

    /* This becomes true only after a fresh, acknowledged OFF1 and status
     * frame from the V20, not merely because an old speed was zero. */
    const bool stop_confirmed =
        ctx->machine.state == STATE_STOP &&
        V20Modbus_StopConfirmed((int64_t)ctx->machine.entered_ms * 1000LL, STOP_CONFIRM_HZ);

    ctx->inputs = (MachineInputs){.now_ms = ctx->now_ms,
                                  .safety = ctx->safety,
                                  .sensor_ok = ctx->status.sensor_ok,
                                  .drive_ready = ctx->motor.ready,
                                  .drive_ok = ctx->motor.ok,
                                  .drive_fault = (ctx->motor.status & 0x0008u) != 0,
                                  .pi_ok = ctx->status.pi_ok,
                                  .stop = ctx->stop,
                                  .start = ctx->start,
                                  .reset = ctx->reset,
                                  .urgent_fault = SafetyIO_Fault(),
                                  .timing_fault = ctx->latched_timing_fault,
                                  .stop_confirmed = stop_confirmed};
}

///////////////////////////////////////////////////////////////////////////////
// static void Control_UpdateStateMachine(ControlContext *ctx)

/*
 * Steps the existing state machine and performs its V20 recovery, arm and disarm actions.
 */
static void Control_UpdateStateMachine(ControlContext *ctx)
{
    const SystemState_t previous_state = ctx->machine.state;
    StateMachine_Step(&ctx->machine, &ctx->inputs);

    if (ctx->machine.recovery_requested)
        V20Modbus_RequestRecheck();

    /* A new physical START is the ONLY place that may arm the VFD. */
    if (previous_state == STATE_READY && ctx->machine.state == STATE_RUNNING)
    {
        const bool feedback_ok = ctx->motor.ok && ctx->motor.ready &&
            isfinite(ctx->motor.actual_hz) && ctx->motor.actual_hz >= 0 &&
            ctx->motor.actual_hz <= MANUAL_MAX_HZ &&
            (TickType_t)(ctx->now_tick - ctx->motor.tick) < pdMS_TO_TICKS(MOTOR_STATUS_TIMEOUT_MS);
        if (ctx->status.mode == MODE_MANUAL && !feedback_ok)
            StateMachine_Fault(&ctx->machine, FAULT_MODBUS, ctx->now_ms);
        else if (!V20Modbus_Arm())
            StateMachine_Fault(&ctx->machine, FAULT_MODBUS, ctx->now_ms);
    }

    /* Disarm immediately on STOP or FAULT; the separate operational K2
     * permissive is kept enabled through a healthy normal OFF1 ramp. */
    if ((previous_state == STATE_RUNNING && ctx->machine.state != STATE_RUNNING) ||
        (previous_state != STATE_FAULT && ctx->machine.state == STATE_FAULT))
        V20Modbus_Disarm();
}

///////////////////////////////////////////////////////////////////////////////
// Controller

///////////////////////////////////////////////////////////////////////////////
// static void Control_UpdateController(ControlContext *ctx)

/*
 * Runs one fixed-period profile and PI step; faults disarm before motor requests are published.
 */
static void Control_UpdateController(ControlContext *ctx)
{
    ctx->status.output = 0.0f;
    if (ctx->machine.state == STATE_RUNNING && ctx->clock_fault == FAULT_NONE)
    {
        if (ctx->status.mode == MODE_MANUAL) {
            if (!ctx->frequency_running) {
                ctx->frequency_running = ctx->motor.ok && isfinite(ctx->motor.actual_hz) &&
                    ctx->motor.actual_hz >= 0 && ctx->motor.actual_hz <= MANUAL_MAX_HZ &&
                    (TickType_t)(ctx->now_tick - ctx->motor.tick) < pdMS_TO_TICKS(MOTOR_STATUS_TIMEOUT_MS) &&
                    SetpointCurve_Init(&ctx->frequency_curve, ctx->motor.actual_hz, 0.005f,
                        MANUAL_MAX_SLEW_HZ_PER_S, MANUAL_MAX_ACCEL_HZ_PER_S2);
                ctx->frequency_curve.max_third_derivative = MANUAL_MAX_JERK_HZ_PER_S3;
            }
            if (!ctx->frequency_running || !SetpointCurve_Step(&ctx->frequency_curve,
                ctx->status.manual_hz, CONTROL_CLOCK_PERIOD_S, &ctx->status.applied_hz)) {
                StateMachine_Fault(&ctx->machine, FAULT_MODBUS, ctx->now_ms);
                V20Modbus_Disarm();
            } else ctx->status.output = 100.0f * ctx->status.applied_hz / V20_REFERENCE_HZ;
        } else {
            if (!ctx->curve_running)
                ctx->curve_running =
                    SetpointCurve_Init(&ctx->curve, ctx->sample.value, SETPOINT_TRANSITION_TIME_S,
                                       SETPOINT_MAX_ACCELERATION_MPS2, SETPOINT_MAX_JERK_MPS3);
            if (!ctx->curve_running ||
                !SetpointCurve_Step(&ctx->curve, ctx->status.setpoint, CONTROL_CLOCK_PERIOD_S,
                                    &ctx->status.control_setpoint) ||
                !PIDregelaar(&ctx->controller, ctx->status.control_setpoint - ctx->sample.value,
                             ctx->sample.value, &ctx->status.output))
            {
                StateMachine_Fault(&ctx->machine, FAULT_INTERNAL, ctx->now_ms);
                V20Modbus_Disarm();
            }
        }
    }
    if (ctx->machine.state != STATE_RUNNING)
    {
        ctx->frequency_running = false;
        ctx->status.applied_hz = 0;
        ctx->curve_running = false;
        ctx->status.control_setpoint = ctx->sample.value;
        ctx->status.output = 0.0f;
        removeRegelaarHistory(&ctx->controller);
    }

    if (ctx->machine.state == STATE_RUNNING && (ctx->motor.status & 0x0040u) != 0 &&
        ctx->now_ms - ctx->machine.entered_ms > RESET_FEEDBACK_WINDOW_MS)
    {
        StateMachine_Fault(&ctx->machine, FAULT_VFD, ctx->now_ms);
        V20Modbus_Disarm();
    }
}

///////////////////////////////////////////////////////////////////////////////
// Motor communication

///////////////////////////////////////////////////////////////////////////////
// static void Control_UpdateMotor(ControlContext *ctx)

/*
 * Services the latest motor request without blocking, then publishes refreshed V20 status.
 */
static void Control_UpdateMotor(ControlContext *ctx)
{
    MotorRequest request = {0};
    (void)xQueuePeek(motor_requests, &request, 0);
    const bool safety_ok = ctx->safety.valid && ctx->safety.commissioned && ctx->safety.relay_ok &&
                           !ctx->safety.estop_pressed && ctx->safety.outputs_ok &&
                           SafetyIO_Fault() == FAULT_NONE;

    const int64_t service_started_us = esp_timer_get_time();
    if (ctx->v20_initialized)
        V20Modbus_Service(&request, safety_ok);
    ctx->status.service_us = (uint32_t)(esp_timer_get_time() - service_started_us);
    if (ctx->status.service_us > ctx->status.service_max_us)
        ctx->status.service_max_us = ctx->status.service_us;

    V20Modbus_GetStatus(&ctx->motor);
    xQueueOverwrite(motor_status, &ctx->motor);
}

///////////////////////////////////////////////////////////////////////////////
// Telemetry and diagnostics

///////////////////////////////////////////////////////////////////////////////
// File-local timing accounting
/*
 * Accounts for jitter and missed notifications without replaying PI steps.
 * Execution and Modbus service budgets retain their original strict limits.
 */
static FaultReason_t ControlTiming_Tick(ControlTiming *t, uint64_t now, unsigned notifications)
{
    uint64_t elapsed = t->previous_us ? now - t->previous_us : CONTROL_PERIOD_US;
    uint64_t jitter =
        elapsed > CONTROL_PERIOD_US ? elapsed - CONTROL_PERIOD_US : CONTROL_PERIOD_US - elapsed;
    if (jitter > t->max_jitter_us)
        t->max_jitter_us = (uint32_t)jitter;
    t->previous_us = now;
    if (!notifications)
    {
        t->missed_deadlines += CONTROL_CLOCK_TIMEOUT_MS / (CONTROL_PERIOD_US / 1000);
        return FAULT_CLOCK_TIMEOUT;
    }
    if (notifications > 1 || elapsed >= 2 * CONTROL_PERIOD_US)
    {
        unsigned missed = notifications > 1 ? notifications - 1 : 0;
        unsigned elapsed_missed = (unsigned)(elapsed / CONTROL_PERIOD_US);
        if (elapsed_missed)
            elapsed_missed--;
        t->missed_deadlines += missed > elapsed_missed ? missed : elapsed_missed;
        return FAULT_CONTROL_OVERRUN;
    }
    return FAULT_NONE;
}
static bool ControlTiming_Execution(ControlTiming *t, uint32_t execution_us, uint32_t service_us)
{
    if (execution_us < CONTROL_PERIOD_US && service_us < V20_SERVICE_BUDGET_US)
        return true;
    t->overruns++;
    return false;
}

///////////////////////////////////////////////////////////////////////////////
// static void Control_PublishStatus(ControlContext *ctx)

/*
 * Publishes current state and motor demand to the existing overwrite queues.
 * Called before Modbus service and again after same-cycle diagnostic fault handling.
 */
static void Control_PublishStatus(ControlContext *ctx)
{
    ctx->status.state = ctx->machine.state;
    ctx->status.fault = ctx->machine.fault;
    ctx->status.last_fault = ctx->machine.last_fault;
    ctx->status.running = ctx->machine.state == STATE_RUNNING;
    ctx->status.reset_required =
        ctx->machine.state == STATE_FAULT || ctx->machine.state == STATE_WAIT;
    ctx->status.reason = ctx->machine.fault == FAULT_NONE ? StateMachine_Name(ctx->machine.state)
                                                          : FaultReason_Name(ctx->machine.fault);
    ctx->status.transitions = ctx->machine.transition_count;
    if (!ctx->status.running)
        ctx->status.output = 0.0f;

    ctx->status.output = fminf(100.0f * MANUAL_MAX_HZ / V20_REFERENCE_HZ,
                              fmaxf(0.0f, ctx->status.output));
    if (ctx->status.mode == MODE_AUTO)
        ctx->status.applied_hz = ctx->status.output * V20_REFERENCE_HZ / 100.0f;
    const MotorRequest command = {.output = ctx->status.output,
                                  .tick = ctx->now_tick,
                                  .run = ctx->status.running,
                                  .permissive = ctx->machine.permissive,
                                  .coast = ctx->machine.state == STATE_FAULT,
                                  .epoch = ctx->machine.epoch};
    xQueueOverwrite(motor_requests, &command);
    xQueueOverwrite(telemetry, &ctx->status);
}

///////////////////////////////////////////////////////////////////////////////
// static void Control_UpdateDiagnostics(ControlContext *ctx)

/*
 * Accounts for execution time and faults immediately on overruns or lost drive communication.
 */
static void Control_UpdateDiagnostics(ControlContext *ctx)
{
    ctx->status.stack_free = uxTaskGetStackHighWaterMark(NULL);
    ctx->status.execution_us = (uint32_t)(esp_timer_get_time() - ctx->started_us);
    if (!ControlTiming_Execution(&ctx->timing_stats, ctx->status.execution_us,
                                 ctx->status.service_us))
    {
        ctx->latched_timing_fault = FAULT_CONTROL_OVERRUN;
        StateMachine_Fault(&ctx->machine, ctx->latched_timing_fault, ctx->now_ms);
        V20Modbus_Disarm();
    }
    if (!ctx->v20_initialized ||
        (!ctx->motor.ok && (ctx->machine.state == STATE_RUNNING ||
                            ctx->machine.state == STATE_READY || ctx->machine.state == STATE_STOP)))
    {
        StateMachine_Fault(&ctx->machine, FAULT_MODBUS, ctx->now_ms);
        V20Modbus_Disarm();
    }
    ctx->status.overruns = ctx->timing_stats.overruns;
}

///////////////////////////////////////////////////////////////////////////////
// static void Control_BeginCycle(ControlContext *ctx, uint32_t notifications)

/*
 * Records the timer wakeup, refreshes the watchdog and latches missed deadlines without replaying
 * steps.
 */
static void Control_BeginCycle(ControlContext *ctx, uint32_t notifications)
{
    ctx->started_us = esp_timer_get_time();
    ctx->now_ms = (uint32_t)(ctx->started_us / 1000);
    ctx->now_tick = xTaskGetTickCount();
    ESP_ERROR_CHECK(esp_task_wdt_reset());

    ctx->clock_fault =
        ControlTiming_Tick(&ctx->timing_stats, (uint64_t)ctx->started_us, notifications);
    if (ctx->clock_fault != FAULT_NONE)
        ctx->latched_timing_fault = ctx->clock_fault;

    ctx->status.max_jitter_us = ctx->timing_stats.max_jitter_us;
    ctx->status.missed_deadlines = ctx->timing_stats.missed_deadlines;
    ctx->cycles++;
    if (ctx->started_us - ctx->window_start_us >= 1000000)
    {
        ctx->status.control_hz =
            (float)ctx->cycles * 1000000.0f / (float)(ctx->started_us - ctx->window_start_us);
        ctx->cycles = 0;
        ctx->window_start_us = ctx->started_us;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Main ControlTask

///////////////////////////////////////////////////////////////////////////////
// void ControlTask(void *argument)

/*
 * Coordinates initialization and the ordered 200 Hz control cycle on Core 1.
 */
void ControlTask(void *argument)
{
    (void)argument;
    ControlContext *ctx = &control;
    Control_Init(ctx);
    Control_WaitForTasks(ctx);
    if (!ctx->tasks_ready)
        Control_HoldInitializationFault(ctx);
    ctx->window_start_us = esp_timer_get_time();
    StartControlClock();

    for (;;)
    {
        const uint32_t notifications =
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
        Control_BeginCycle(ctx, notifications);
        const EventBits_t buttonBits = Control_ReadButtonEvents();
        Control_ReadInputs(ctx);
        Control_ProcessCommands(ctx, buttonBits);
        Control_CheckSafety(ctx);
        Control_UpdateStateMachine(ctx);
        Control_UpdateController(ctx);
        Control_PublishStatus(ctx);
        Control_UpdateMotor(ctx);
        Control_UpdateDiagnostics(ctx);
        Control_PublishStatus(ctx);
    }
}
