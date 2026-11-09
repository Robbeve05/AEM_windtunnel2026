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

#include "driver/gptimer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "ApplicationTasks.h"
#include "DigitalOutputs.h"
#include "Ethernet.h"
#include "FanCalibration.h"
#include "Regelaar.h"
#include "TorqueModel.h"
#include "FrequencyModel.h"
#include "PiProtocol.h"
#include "PressureSensors.h"
#include "SafetyIO.h"
#include "SetpointCurve.h"
#include "TaskData.h"
#include "V20Modbus.h"

///////////////////////////////////////////////////////////////////////////////
// Local definitions

_Static_assert(CONTROL_PERIOD_US == (1000000u / CONTROL_FREQUENCY_HZ),
               "GPTimer and torque PID sample periods must be equal");
_Static_assert(SENSOR_PHASE_US * 2 == CONTROL_PERIOD_US, "Two equal clock phases required");
_Static_assert(CONTROL_FREQUENCY_HZ == 100u,
               "The configured wind tunnel controller runs at 100 Hz");
_Static_assert(CONTROL_CLOCK_MISSED_TICKS_ALLOWED <= 1u,
               "At most one consecutive control-clock tick may be tolerated");

///////////////////////////////////////////////////////////////////////////////
// File-local data structures

typedef struct
{
    uint64_t previous_us;
    uint32_t max_jitter_us, missed_deadlines, overruns, consecutive_missed_ticks;
    uint32_t fault_execution_us, fault_service_us, fault_missed_ticks;
    FaultReason_t fault_reason;
} ControlTiming;

typedef struct
{
    Regelaar controller;
#if CONTROL_TYPE == CONTROL_TORQUE_PI
    TorqueModel torque_model;
    bool torque_model_ready;
#elif CONTROL_TYPE == CONTROL_FREQUENCY_PI
    FrequencyModel frequency_model;
    bool frequency_controller_ready;
#endif
    SetpointCurve curve;
    SetpointCurve frequency_curve;
    bool frequency_running;
    StateMachine machine;
    Telemetry status;
    ControlTiming timing_stats;
    PitotSample sample;
    SafetySnapshot safety;
    PiHeartbeat heartbeat;
    MotorStatus motor;
    FaultReason_t latched_timing_fault;
    uint32_t previous_start_sequence;
    uint32_t previous_reset_sequence;
    bool curve_running;
    float previous_ff_setpoint;
    bool v20_initialized;
    uint32_t v20_init_retry_ms;
    bool control_clock_started;
    uint32_t cycles;
    uint32_t control_cycle_count;
    int64_t window_start_us;

    /* Per-cycle values shared by the ordered processing stages. */
    int64_t started_us;
    uint32_t now_ms;
    TickType_t now_tick;
    FaultReason_t clock_fault;
    MachineInputs inputs;
    bool start, stop, reset;
    bool tasks_ready;
    FaultEvent history[FAULT_HISTORY_SIZE];
    unsigned history_next, history_count;
    uint32_t fault_sequence, previous_disconnect_sequence;
    bool disconnected_since_read, ethernet_link, fault_episode_active;
} ControlContext;

///////////////////////////////////////////////////////////////////////////////
// File-local variables

/* Static storage preserves the existing task stack budget. */
static ControlContext control;

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static bool ControlClockInterrupt(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event,
                                  void *argument);
static void StartControlClock(void);
static void Control_Init(ControlContext *ctx);
static void Control_WaitForTasks(ControlContext *ctx);
static void Control_HoldInitializationFault(ControlContext *ctx);
static EventBits_t Control_ReadButtonEvents(void);
static void Control_ReadInputs(ControlContext *ctx);
static void Control_AcquirePitot(ControlContext *ctx);
static void Control_ProcessCommands(ControlContext *ctx, EventBits_t buttonBits);
static void Control_CheckSafety(ControlContext *ctx);
static void Control_UpdateConfiguration(ControlContext *ctx);
static void Control_UpdateStateMachine(ControlContext *ctx);
static void Control_UpdateController(ControlContext *ctx);
static void Control_UpdateMotor(ControlContext *ctx);
static FaultReason_t ControlTiming_Tick(ControlTiming *t, uint64_t now, unsigned notifications);
static bool ControlTiming_Execution(ControlTiming *t, uint32_t execution_us, uint32_t service_us);
static void Control_PublishStatus(ControlContext *ctx);
static void Control_UpdateDiagnostics(ControlContext *ctx);
static void Control_BeginCycle(ControlContext *ctx, uint32_t notifications);
static void Control_RecordEvent(ControlContext *ctx);
static void Control_CaptureFault(ControlContext *ctx, SystemState_t previous, const char *source);
static void Control_Fault(ControlContext *ctx, FaultReason_t fault, const char *source);

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// StartControlClock()

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
// Control_Init()

/*
 * Initializes control, diagnostics and disabled motor commands before startup
 * synchronization.
 */
static void Control_Init(ControlContext *ctx)
{
    *ctx = (ControlContext){0};
    (void)SensorLib_Init(); /* Pitot state has one owner, initialized before the timer. */
    control_task_handle = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    RegelaarParameters parameters = {0};
#if CONTROL_TYPE == CONTROL_TORQUE_PI
    const TorqueDesignInput input = {.jm = J_MOTOR_KGM2,
                                     .jfan = J_FAN_KGM2,
                                     .frequency_hz = DISTURBANCE_FREQUENCY_HZ,
                                     .disturbance_ms = DISTURBANCE_AMPLITUDE_MS,
                                     .error_ms = ALLOWABLE_ERROR_MS,
                                     .nominal_rpm = DESIGN_NOMINAL_RPM,
                                     .nominal_airspeed_ms = LUCHTSNELHEID_NOM,
                                     .sample_s = CONTROL_CLOCK_PERIOD_S,
                                     .minimum_pm_deg = DESIGN_MIN_PHASE_MARGIN_DEG};
    TorqueDesignOutput design = {0};
    if (TorqueModel_Design(&input, &design))
    {
        ctx->torque_model =
            (TorqueModel){.inertia_kgm2 = design.j,
                          .slip_gain_nm_per_rad_s = MOTOR_MODEL_SLIP_GAIN_NM_PER_RAD_S,
                          .load_coeff_nm_per_rad_s2 = MOTOR_MODEL_FAN_LOAD_NM_PER_RAD_S2,
                          .pole_pairs = MOTOR_MODEL_POLE_PAIRS,
                          .nominal_air_omega_gain = design.kv,
                          .max_command_hz = MANUAL_MAX_HZ};
        parameters = (RegelaarParameters){.kp = design.kp,
                                          .ti_s = design.ti,
                                          .td_s = design.td,
                                          .tf_s = design.tf,
                                          .sample_s = CONTROL_CLOCK_PERIOD_S,
                                          .mode = (RegelaarMode)CONTROLLER_SETTING};
        TorqueModelResult check;
        const TorqueModelRequest zero = {0};
        ctx->torque_model_ready = TorqueModel_Inverse(&ctx->torque_model, &zero, &check) &&
                                  Regelaar_Init(&ctx->controller, &parameters);
        ESP_LOGI("TORQUE_PID", "J=%g Kp=%g Ti=%g Td=%g Tf=%g; inverse map unverified",
                 (double)design.j, (double)parameters.kp, (double)parameters.ti_s,
                 (double)parameters.td_s, (double)parameters.tf_s);
    }
#elif CONTROL_TYPE == CONTROL_FREQUENCY_PI
    ctx->frequency_model = (FrequencyModel){.gain_mps_per_hz = FREQUENCY_MODEL_GAIN_MPS_PER_HZ,
                                            .tau_s = FREQUENCY_MODEL_TAU_S,
                                            .delay_s = FREQUENCY_MODEL_DELAY_S,
                                            .lambda_s = FREQUENCY_MODEL_LAMBDA_S,
                                            .max_hz = MANUAL_MAX_HZ,
                                            .identified_valid = FREQUENCY_MODEL_VALIDATED,
                                            .commissioned = FREQUENCY_MODEL_COMMISSIONED};
    ctx->frequency_controller_ready =
        FrequencyModel_Design(&ctx->frequency_model, CONTROL_CLOCK_PERIOD_S, &parameters) &&
        Regelaar_Init(&ctx->controller, &parameters);
#endif
    ctx->now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ctx->now_tick = xTaskGetTickCount();
    StateMachine_Init(&ctx->machine, ctx->now_ms);
    ctx->v20_initialized = V20Modbus_Init();
    if (ctx->v20_initialized)
        ESP_LOGI("Control", "V20 Modbus UART initialization successful; drive "
                            "communication pending");
    else
    {
        ESP_LOGE("Control", "V20 Modbus UART initialization failed; motor remains disarmed");
        Control_Fault(ctx, FAULT_MODBUS, "control");
    }
    V20Modbus_Disarm();
    ctx->window_start_us = esp_timer_get_time();
    Control_PublishStatus(ctx);
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// ControlClockInterrupt()

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
 * Starts the alternating 5 ms sensor/control phase timer after startup
 * synchronization.
 */

///////////////////////////////////////////////////////////////////////////////
// Control_WaitForTasks()

/*
 * Waits within the existing initialization deadline; ready bits remain set.
 * Readiness means task initialization completed, not that external hardware is
 * healthy.
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
    Control_Fault(ctx, FAULT_INITIALIZATION, "control");
    V20Modbus_Disarm();
    Control_PublishStatus(ctx);
}

///////////////////////////////////////////////////////////////////////////////
// Control_HoldInitializationFault()

/*
 * Keeps failed startup disabled while required services recover, without
 * starting the control timer. Publishes diagnostics and resumes INIT only
 * after the task barrier is healthy; RESET/START are never inferred.
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
        Control_Fault(ctx, SafetyIO_Fault(), "control");
        V20Modbus_Disarm();
        Control_PublishStatus(ctx);
        Control_UpdateMotor(ctx);
        Control_PublishStatus(ctx);
        if ((xEventGroupGetBits(handle_ThreadEventGroup) & EVT_ALL_TASKS_READY) == EVT_ALL_TASKS_READY)
        {
            /* Restored initialization is not reset acceptance or run permission. */
            ctx->tasks_ready = true;
            StateMachine_Init(&ctx->machine, ctx->now_ms);
            ctx->previous_start_sequence = ctx->safety.start_sequence;
            ctx->previous_reset_sequence = ctx->safety.reset_sequence;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
    }
}

///////////////////////////////////////////////////////////////////////////////
// Control_ReadButtonEvents()

/*
 * Consumes one-shot physical button notifications without waiting or reading
 * GPIOs.
 */
static EventBits_t Control_ReadButtonEvents(void)
{
    return xEventGroupWaitBits(handle_ButtonEventGroup, EVT_ALL_BUTTONS, pdTRUE, pdFALSE, 0) &
           EVT_ALL_BUTTONS;
}

///////////////////////////////////////////////////////////////////////////////
// Control_ReadInputs()

/*
 * Reads and validates latest queue values every cycle, even without advisory
 * update bits.
 */
static void Control_ReadInputs(ControlContext *ctx)
{
    (void)xEventGroupWaitBits(handle_SystemEventGroup, EVT_NEW_COMMAND | EVT_SENSOR_UPDATED, pdTRUE,
                              pdFALSE, 0);
    ctx->safety = (SafetySnapshot){0};
    ctx->heartbeat = (PiHeartbeat){0};
    ctx->motor = (MotorStatus){0};
    (void)xQueuePeek(safety_status, &ctx->safety, 0);
    (void)xQueuePeek(pi_heartbeat, &ctx->heartbeat, 0);
    V20Modbus_GetStatus(&ctx->motor);

    ctx->status.measurement = ctx->sample.value;
    /* Producers on core 0 can publish AFTER BeginCycle. Read the clock only
     * after the queue reads, or a new timestamp underflows unsigned age. */
    const TickType_t observed_tick = xTaskGetTickCount();
    ctx->now_tick = observed_tick;
    ctx->now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ctx->status.sensor_ok = ctx->sample.valid && (TickType_t)(observed_tick - ctx->sample.tick) <
                                                     pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
    ctx->status.motor_ok = ctx->motor.operational_ready && ctx->motor.ok;
    ctx->disconnected_since_read =
        ctx->heartbeat.disconnect_sequence != ctx->previous_disconnect_sequence;
    ctx->previous_disconnect_sequence = ctx->heartbeat.disconnect_sequence;
    ctx->ethernet_link = Ethernet_LinkUp();
    ctx->status.pi_ok =
        !PI_HEARTBEAT_REQUIRED ||
        (ctx->ethernet_link && !ctx->disconnected_since_read &&
         ctx->heartbeat.tcp_connected && ctx->heartbeat.connected &&
         (TickType_t)(observed_tick - ctx->heartbeat.tick) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS));
    ctx->status.safety = ctx->safety;
}

///////////////////////////////////////////////////////////////////////////////
// Control_AcquirePitot()

static void Control_AcquirePitot(ControlContext *ctx)
{
    /* STOP/emergency never wait for I2C. The old timestamp is retained on this
     * skipped cycle, never republished as a new measurement or used for ISA. */
    if (ctx->stop || ctx->safety.estop_pressed || ctx->clock_fault != FAULT_NONE || SafetyIO_Fault() != FAULT_NONE)
        return;
    ctx->sample = (PitotSample){0};
    (void)SensorLib_PitotStep(&ctx->sample);
    ctx->now_tick = xTaskGetTickCount();
    ctx->now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ctx->status.measurement = ctx->sample.value;
    ctx->status.sensor_ok = ctx->sample.valid &&
        (TickType_t)(ctx->now_tick - ctx->sample.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
    ctx->status.pitot_acquisition_us = ctx->sample.acquisition_us;
    if (ctx->sample.acquisition_us > ctx->status.pitot_acquisition_max_us)
        ctx->status.pitot_acquisition_max_us = ctx->sample.acquisition_us;
    xQueueOverwrite(pitot_samples, &ctx->sample);
    /* Account for safety/heartbeat changes during the bounded I2C call. Do not
     * extend a heartbeat that expired while this core waited for the driver. */
    ctx->status.pi_ok = ctx->status.pi_ok && (!PI_HEARTBEAT_REQUIRED ||
        (Ethernet_LinkUp() && (TickType_t)(ctx->now_tick - ctx->heartbeat.tick) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)));
    (void)xQueuePeek(safety_status, &ctx->safety, 0);
    ctx->status.safety = ctx->safety;
    ctx->stop = ctx->stop || SafetyIO_Stop() ||
        (xEventGroupWaitBits(events, STOP_REQUEST, pdTRUE, pdFALSE, 0) & STOP_REQUEST) != 0;
}

///////////////////////////////////////////////////////////////////////////////
// Control_ProcessCommands()

/*
 * Processes physical actions and at most sixteen queued commands, retaining all
 * START interlocks.
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
    bool calibration_busy = FanCalibration_ControlGate(
        ctx->machine.state == STATE_READY && ctx->motor.ok && ctx->motor.ready &&
        (TickType_t)(ctx->now_tick - ctx->motor.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS) &&
        fabsf(ctx->motor.actual_hz) <= 0.2f);
    ctx->start = ctx->start && !calibration_busy && ctx->machine.state == STATE_READY &&
                 (int32_t)(ctx->safety.start_ms - ctx->machine.entered_ms) > 0;

    ctx->stop = SafetyIO_Stop() || (buttonBits & EVT_STOP_BUTTON) != 0 ||
                (xEventGroupWaitBits(events, STOP_REQUEST, pdTRUE, pdFALSE, 0) & STOP_REQUEST) != 0;

    PiCommand command;
    for (unsigned i = 0; i < 16 && xQueueReceive(commands, &command, 0) == pdTRUE; ++i)
    {
        switch (command.type)
        {
        case CMD_MODE:
            if (ctx->machine.state != STATE_STARTING && ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STOP)
            {
                ctx->status.mode = (OperatingMode)(int)command.values[0];
                ctx->status.manual_hz = ctx->status.applied_hz = ctx->status.output = 0;
                ctx->frequency_running = ctx->curve_running = false;
                Regelaar_Reset(&ctx->controller);
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
#if CONTROL_TYPE == CONTROL_TORQUE_PI
            if (ctx->machine.state != STATE_STARTING && ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STOP)
            {
                RegelaarParameters parameters;
                /* Wire protocol remains legacy Kp/Ki/Kd, converted at this boundary. */
                if (Regelaar_FromLegacy(command.values[0], command.values[1], command.values[2],
                                        ctx->controller.parameters.tf_s, CONTROL_CLOCK_PERIOD_S,
                                        (RegelaarMode)CONTROLLER_SETTING, &parameters))
                    (void)Regelaar_SetParameters(&ctx->controller, &parameters);
            }
#endif
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
// Control_UpdateConfiguration()

static void Control_UpdateConfiguration(ControlContext *ctx)
{
    ConfigurationDiagnostics *d = &ctx->status.configuration;
    *d = (ConfigurationDiagnostics){.failure_reason = "NONE", .mode = ctx->status.mode,
        .control_type = CONTROL_TYPE, .watchdog_mapping_required = V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED};
#if CONTROL_TYPE == CONTROL_TORQUE_PI
    d->controller_ready = ctx->torque_model_ready;
    d->model_commissioned = SIMULATION_MODE || MOTOR_MODEL_TORQUE_TO_V20_ENABLED;
#elif CONTROL_TYPE == CONTROL_FREQUENCY_PI
    d->controller_ready = ctx->frequency_controller_ready;
    d->calibration_valid = FanCalibration_IsValid();
    d->model_identified = ctx->frequency_model.identified_valid;
    d->model_commissioned = SIMULATION_MODE || ctx->frequency_model.commissioned;
    d->model_gain = ctx->frequency_model.gain_mps_per_hz;
    d->model_tau_s = ctx->frequency_model.tau_s;
    d->model_delay_s = ctx->frequency_model.delay_s;
    d->model_lambda_s = ctx->frequency_model.lambda_s;
#endif
    if (!ctx->motor.reference_config_valid)
        d->failure_reason = "V20_REFERENCE_INVALID";
    else if (V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED && !ctx->motor.watchdog_config_known)
        d->failure_reason = "V20_WATCHDOG_UNVERIFIED";
    else if (V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED && !ctx->motor.watchdog_config_valid)
        d->failure_reason = "V20_WATCHDOG_INVALID";
    else if (ctx->status.mode != MODE_AUTO && ctx->status.mode != MODE_MANUAL)
        d->failure_reason = "OPERATING_MODE_INVALID";
    else if (ctx->status.mode == MODE_AUTO)
    {
#if CONTROL_TYPE == CONTROL_TORQUE_PI
        if (!d->controller_ready) d->failure_reason = "TORQUE_CONTROLLER_INVALID";
        else if (!d->model_commissioned) d->failure_reason = "TORQUE_MODEL_NOT_COMMISSIONED";
#elif CONTROL_TYPE == CONTROL_FREQUENCY_PI
        if (!d->model_identified) d->failure_reason = "FREQUENCY_MODEL_NOT_IDENTIFIED";
        else if (!isfinite(d->model_gain) || d->model_gain <= 0)
            d->failure_reason = "FREQUENCY_MODEL_GAIN_INVALID";
        else if (!isfinite(d->model_tau_s) || d->model_tau_s <= 0)
            d->failure_reason = "FREQUENCY_MODEL_TAU_INVALID";
        else if (!isfinite(d->model_delay_s) || d->model_delay_s < 0)
            d->failure_reason = "FREQUENCY_MODEL_DELAY_INVALID";
        else if (!isfinite(d->model_lambda_s) || d->model_lambda_s <= 0)
            d->failure_reason = "FREQUENCY_MODEL_LAMBDA_INVALID";
        else if (!d->controller_ready) d->failure_reason = "FREQUENCY_CONTROLLER_INVALID";
        else if (!d->calibration_valid) d->failure_reason = "FAN_CALIBRATION_MISSING";
        else if (!d->model_commissioned) d->failure_reason = "FREQUENCY_MODEL_NOT_COMMISSIONED";
#endif
    }
    d->start_ready = !strcmp(d->failure_reason, "NONE");
}

///////////////////////////////////////////////////////////////////////////////
// Control_CheckSafety()

/*
 * Builds state-machine inputs and preserves physical reset acknowledgement and
 * fault priorities.
 */
static void Control_CheckSafety(ControlContext *ctx)
{
    Control_UpdateConfiguration(ctx);
    /* Physical reset clears a timing latch only when the input is healthy. */
    if (ctx->reset && ctx->clock_fault == FAULT_NONE && ctx->safety.valid &&
        !ctx->safety.estop_pressed &&
        (ctx->machine.state == STATE_WAIT || ctx->machine.state == STATE_FAULT))
    {
        ctx->latched_timing_fault = FAULT_NONE;
        ctx->timing_stats.fault_reason = FAULT_NONE;
        ctx->timing_stats.fault_execution_us = 0;
        ctx->timing_stats.fault_service_us = 0;
        ctx->timing_stats.fault_missed_ticks = 0;
    }

    if (ctx->reset && (ctx->machine.state == STATE_WAIT || ctx->machine.state == STATE_FAULT) &&
        ctx->safety.valid && ctx->safety.commissioned && ctx->safety.outputs_ok &&
        ctx->now_ms - ctx->safety.tick_ms < INPUT_TIMEOUT_MS &&
        !ctx->safety.estop_pressed && !ctx->safety.reset_pressed && ctx->safety.stop_ok &&
        ctx->safety.relay_ok &&
        (!ctx->safety.contactor_available || ctx->safety.contactor_ok))
        SafetyIO_Acknowledge();

    /* This becomes true only after a fresh, acknowledged OFF1 and status
     * frame from the V20, not merely because an old speed was zero. */
    const bool stop_confirmed =
        ctx->machine.state == STATE_STOP &&
        V20Modbus_StopConfirmed((int64_t)ctx->machine.entered_ms * 1000LL, STOP_CONFIRM_HZ);

    ctx->inputs =
        (MachineInputs){.now_ms = ctx->now_ms,
                        .safety = ctx->safety,
                        .sensor_ok = ctx->status.sensor_ok,
                        .drive_ready = ctx->motor.operational_ready,
                        .drive_ok = ctx->motor.ok,
                        .drive_fault_free = ctx->motor.drive_fault_free,
                        .drive_fault = ctx->motor.status_valid && (ctx->motor.status & 0x0008u) != 0,
                        .drive_run_locked = ctx->motor.run_locked,
                        .drive_operation_enabled = ctx->motor.operation_enabled,
                        .stop_command_acknowledged = ctx->motor.stopped_command_acknowledged,
                        .drive_communication_ok = ctx->motor.communication_ok,
                        .v20_response_received = ctx->motor.status_valid,
                        .v20_status_healthy = ctx->motor.status_healthy,
                        .v20_modbus_initialized = ctx->motor.initialized,
                        .v20_run_commissioned = V20_RUN_COMMISSIONED,
                        .v20_diagnostics_complete = ctx->motor.diagnostics_complete,
                        .v20_configuration_valid = ctx->motor.reference_config_valid &&
                                                   (!V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED ||
                                                    ctx->motor.watchdog_config_valid),
                        .start_configuration_invalid = !ctx->status.configuration.start_ready,
                        .pi_ok = ctx->status.pi_ok,
                        .stop = ctx->stop,
                        .start = ctx->start,
                        .reset = ctx->reset,
                        .urgent_fault = SafetyIO_Fault(),
                        .timing_fault = ctx->latched_timing_fault,
                        .stop_confirmed = stop_confirmed,
                        .stopped_prepared = V20Modbus_StopConfirmed(
                            (int64_t)ctx->machine.entered_ms * 1000LL, STOP_CONFIRM_HZ),
                        .start_confirmed = V20Modbus_StartConfirmed(
                            (int64_t)ctx->machine.entered_ms * 1000LL)};
}

///////////////////////////////////////////////////////////////////////////////
// Control_UpdateStateMachine()

/*
 * Steps the existing state machine and performs its V20 recovery, arm and
 * disarm actions.
 */
static void Control_UpdateStateMachine(ControlContext *ctx)
{
    const SystemState_t previous_state = ctx->machine.state;
    StateMachine_Step(&ctx->machine, &ctx->inputs);
    if (ctx->machine.state == STATE_FAULT)
        Control_CaptureFault(ctx, previous_state,
            ctx->machine.fault == FAULT_CONFIGURATION && ctx->inputs.start_configuration_invalid ?
                "control_configuration" :
            ctx->machine.fault != FAULT_PI_CONNECTION ? "state_machine" :
            !ctx->ethernet_link ? "pi_link_down" :
            (ctx->disconnected_since_read || !ctx->heartbeat.tcp_connected) ? "pi_tcp_disconnect" :
            "pi_heartbeat");
    else if (previous_state == STATE_FAULT)
    {
        /* Only the existing physical-reset state-machine path can leave FAULT. */
        ctx->fault_episode_active = false;
        /* Keep archived evidence available to slow log readers after RESET.
         * A later, distinct fault episode will replace it with a new sequence. */
    }

    if (ctx->machine.recovery_requested)
    {
        V20Modbus_RequestRecheck();
        (void)V20Modbus_RequestFaultAck();
    }
    if (previous_state != STATE_STOPPED_PREPARE && ctx->machine.state == STATE_STOPPED_PREPARE)
        V20Modbus_Disarm();

    /* A new physical START is the ONLY place that may arm the VFD. */
    if (previous_state == STATE_READY && ctx->machine.state == STATE_STARTING)
    {
        const bool feedback_ok =
            ctx->motor.ok && ctx->motor.ready && isfinite(ctx->motor.actual_hz) &&
            ctx->motor.actual_hz >= 0 && ctx->motor.actual_hz <= MANUAL_MAX_HZ &&
            (TickType_t)(ctx->now_tick - ctx->motor.tick) < pdMS_TO_TICKS(MOTOR_STATUS_TIMEOUT_MS);
        if (ctx->status.mode == MODE_MANUAL && !feedback_ok)
            Control_Fault(ctx, FAULT_MODBUS, "control");
        else if (!V20Modbus_Arm())
            Control_Fault(ctx, FAULT_MODBUS, "control");
    }

    /* Disarm immediately on STOP or FAULT; the separate operational VFD-enable
     * relay permissive is kept enabled through a healthy normal OFF1 ramp. */
    if (((previous_state == STATE_RUNNING || previous_state == STATE_STARTING) &&
         ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STARTING) ||
        (previous_state != STATE_FAULT && ctx->machine.state == STATE_FAULT))
        V20Modbus_Disarm();
}

///////////////////////////////////////////////////////////////////////////////
// Control_UpdateController()

/*
 * Runs one fixed-period profile and PI step; faults disarm before motor
 * requests are published.
 */
static void Control_UpdateController(ControlContext *ctx)
{
    ctx->status.output = 0.0f;
    if (ctx->machine.state == STATE_RUNNING && !ctx->stop && !ctx->status.sensor_ok)
    {
        Control_Fault(ctx, FAULT_SENSOR, "pitot_feedback");
        V20Modbus_Disarm();
    }
    if (ctx->machine.state == STATE_RUNNING && !ctx->stop && ctx->status.sensor_ok && ctx->clock_fault == FAULT_NONE)
    {
        if (ctx->status.mode == MODE_MANUAL)
        {
            if (!ctx->frequency_running)
            {
                ctx->frequency_running =
                    ctx->motor.ok && isfinite(ctx->motor.actual_hz) && ctx->motor.actual_hz >= 0 &&
                    ctx->motor.actual_hz <= MANUAL_MAX_HZ &&
                    (TickType_t)(ctx->now_tick - ctx->motor.tick) <
                        pdMS_TO_TICKS(MOTOR_STATUS_TIMEOUT_MS) &&
                    SetpointCurve_Init(&ctx->frequency_curve, ctx->motor.actual_hz,
                                       MANUAL_TRANSITION_TIME_S, MANUAL_MAX_SLEW_HZ_PER_S,
                                       MANUAL_MAX_ACCEL_HZ_PER_S2);
                ctx->frequency_curve.max_third_derivative = MANUAL_MAX_JERK_HZ_PER_S3;
            }
            if (!ctx->frequency_running ||
                !SetpointCurve_Step(&ctx->frequency_curve, ctx->status.manual_hz,
                                    CONTROL_CLOCK_PERIOD_S, &ctx->status.applied_hz))
            {
                Control_Fault(ctx, FAULT_MODBUS, "control");
                V20Modbus_Disarm();
            }
            else
                ctx->status.output = 100.0f * ctx->status.applied_hz / V20_REFERENCE_HZ;
        }
        else
        {
            if (!ctx->curve_running)
                ctx->curve_running =
                    SetpointCurve_Init(&ctx->curve, ctx->sample.value, SETPOINT_TRANSITION_TIME_S,
                                       SETPOINT_MAX_ACCELERATION_MPS2, SETPOINT_MAX_JERK_MPS3);
            if (ctx->curve_running && ctx->curve.elapsed == 0.0)
                ctx->previous_ff_setpoint = ctx->sample.value;
            bool valid = ctx->curve_running &&
                         SetpointCurve_Step(&ctx->curve, ctx->status.setpoint,
                                            CONTROL_CLOCK_PERIOD_S, &ctx->status.control_setpoint);
            RegelaarOutput correction = {0};
            const bool fresh_feedback =
                ctx->motor.ok && ctx->motor.status_valid && isfinite(ctx->motor.actual_hz) &&
                ctx->motor.actual_hz >= 0 && ctx->motor.actual_hz <= MANUAL_MAX_HZ &&
                (TickType_t)(ctx->now_tick - ctx->motor.tick) <
                    pdMS_TO_TICKS(MOTOR_STATUS_TIMEOUT_MS);
#if CONTROL_TYPE == CONTROL_TORQUE_PI
            valid = valid && ctx->torque_model_ready &&
                    (SIMULATION_MODE || MOTOR_MODEL_TORQUE_TO_V20_ENABLED);
#elif CONTROL_TYPE == CONTROL_FREQUENCY_PI
            valid = valid && ctx->frequency_controller_ready &&
                    (SIMULATION_MODE || ctx->frequency_model.commissioned);
#endif
            valid = valid && isfinite(ctx->sample.value) && ctx->sample.value >= 0.0f &&
                    Regelaar_Step(&ctx->controller, ctx->status.control_setpoint, ctx->sample.value,
                                  &correction);
#if CONTROL_TYPE == CONTROL_TORQUE_PI
            if (valid)
            {
                const float acceleration =
                    (ctx->status.control_setpoint - ctx->previous_ff_setpoint) /
                    CONTROL_CLOCK_PERIOD_S;
                TorqueModelResult result;
                valid =
                    TorqueModel_Apply(&ctx->torque_model, ctx->status.control_setpoint,
                                      ctx->sample.value, acceleration, correction.value, &result);
                if (valid &&
                    TorqueModel_RejectIntegration(&result, ctx->torque_model.max_command_hz,
                                                  correction.integral_increment, fresh_feedback,
                                                  ctx->motor.actual_hz))
                {
                    valid = Regelaar_RejectLatestIntegration(&ctx->controller, &correction) &&
                            TorqueModel_Apply(&ctx->torque_model, ctx->status.control_setpoint,
                                              ctx->sample.value, acceleration, correction.value,
                                              &result);
                }
                if (valid)
                {
                    ctx->status.applied_hz = result.frequency_hz;
                    ctx->status.output = 100 * result.frequency_hz / V20_REFERENCE_HZ;
                }
            }
#elif CONTROL_TYPE == CONTROL_FREQUENCY_PI
            if (valid)
            {
                FrequencyModelOutput result;
                valid = FrequencyModel_Apply(&ctx->frequency_model, ctx->status.control_setpoint,
                                             correction.value, &result);
                if (valid && FrequencyModel_RejectIntegration(&result, ctx->frequency_model.max_hz,
                                                              correction.integral_increment,
                                                              fresh_feedback, ctx->motor.actual_hz))
                {
                    valid =
                        Regelaar_RejectLatestIntegration(&ctx->controller, &correction) &&
                        FrequencyModel_Apply(&ctx->frequency_model, ctx->status.control_setpoint,
                                             correction.value, &result);
                }
                if (valid)
                {
                    ctx->status.applied_hz = result.frequency_hz;
                    ctx->status.output = 100 * result.frequency_hz / V20_REFERENCE_HZ;
                }
            }
#endif
            if (!valid)
            {
                Control_Fault(ctx, FAULT_INTERNAL, "control");
                V20Modbus_Disarm();
                ctx->status.output = 0.0f;
            }
            ctx->previous_ff_setpoint = ctx->status.control_setpoint;
        }
    }
    if (ctx->machine.state != STATE_RUNNING)
    {
        ctx->frequency_running = false;
        ctx->status.applied_hz = 0;
        ctx->curve_running = false;
        ctx->previous_ff_setpoint = ctx->sample.value;
        ctx->status.control_setpoint = ctx->sample.value;
        ctx->status.output = 0.0f;
        Regelaar_Reset(&ctx->controller);
    }

    if (ctx->machine.state == STATE_RUNNING && (ctx->motor.status & 0x0040u) != 0 &&
        ctx->now_ms - ctx->machine.entered_ms > RESET_FEEDBACK_WINDOW_MS)
    {
        Control_Fault(ctx, FAULT_VFD, "control");
        V20Modbus_Disarm();
    }
}

///////////////////////////////////////////////////////////////////////////////
// Control_UpdateMotor()

/*
 * Services the latest motor request without blocking, then publishes refreshed
 * V20 status.
 */
static void Control_UpdateMotor(ControlContext *ctx)
{
    MotorRequest request = {0};
    (void)xQueuePeek(motor_requests, &request, 0);
    const bool safety_ok = ctx->safety.valid && ctx->safety.commissioned && ctx->safety.relay_ok &&
                           !ctx->safety.estop_pressed && ctx->safety.outputs_ok &&
                           SafetyIO_Fault() == FAULT_NONE;

    const int64_t service_started_us = esp_timer_get_time();
    if (!ctx->v20_initialized && ctx->now_ms - ctx->v20_init_retry_ms >= 1000u)
    {
        ctx->v20_init_retry_ms = ctx->now_ms;
        ctx->v20_initialized = V20Modbus_Init();
        V20Modbus_Disarm();
    }
    if (ctx->v20_initialized)
        V20Modbus_Service(&request, safety_ok);
    ctx->status.service_us = (uint32_t)(esp_timer_get_time() - service_started_us);
    if (ctx->status.service_us > ctx->status.service_max_us)
        ctx->status.service_max_us = ctx->status.service_us;

    V20Modbus_GetStatus(&ctx->motor);
    xQueueOverwrite(motor_status, &ctx->motor);
}

///////////////////////////////////////////////////////////////////////////////
// File-local timing accounting

/*
 * Accounts for jitter and missed notifications without replaying PI steps.
 * Execution and Modbus service budgets retain their original strict limits.
 */

///////////////////////////////////////////////////////////////////////////////
// ControlTiming_Tick()

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
        t->consecutive_missed_ticks = 0;
        if (t->fault_reason == FAULT_NONE)
        {
            t->fault_reason = FAULT_CLOCK_TIMEOUT;
            t->fault_missed_ticks = CONTROL_CLOCK_TIMEOUT_MS / (CONTROL_PERIOD_US / 1000);
        }
        return FAULT_CLOCK_TIMEOUT;
    }
    unsigned missed = notifications > 1 ? notifications - 1 : 0;
    unsigned elapsed_missed = (unsigned)(elapsed / CONTROL_PERIOD_US);
    if (elapsed_missed)
        elapsed_missed--;
    if (elapsed_missed > missed)
        missed = elapsed_missed;
    if (missed)
    {
        t->missed_deadlines += missed;
        t->consecutive_missed_ticks += missed;
        if (t->consecutive_missed_ticks > CONTROL_CLOCK_MISSED_TICKS_ALLOWED)
        {
            if (t->fault_reason == FAULT_NONE)
            {
                t->fault_reason = FAULT_CONTROL_OVERRUN;
                t->fault_missed_ticks = t->consecutive_missed_ticks;
            }
            return FAULT_CONTROL_OVERRUN;
        }
    }
    else
    {
        t->consecutive_missed_ticks = 0;
    }
    return FAULT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// ControlTiming_Execution()

static bool ControlTiming_Execution(ControlTiming *t, uint32_t execution_us, uint32_t service_us)
{
    if (execution_us < CONTROL_PERIOD_US && service_us < V20_SERVICE_BUDGET_US)
        return true;
    t->overruns++;
    if (t->fault_reason == FAULT_NONE)
    {
        t->fault_reason = FAULT_CONTROL_OVERRUN;
        t->fault_execution_us = execution_us;
        t->fault_service_us = service_us;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////
// Control_RecordEvent()

static void Control_RecordEvent(ControlContext *ctx)
{
    DigitalOutputStatus outputs = {0};
    DigitalOutputs_GetStatus(&outputs);
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const FaultEvent event = {.timestamp_ms = now, .state = ctx->machine.state,
        .statusword = ctx->motor.status, .status_valid = ctx->motor.status_valid,
        .do1_requested = outputs.vfd_requested, .do1_acknowledged = outputs.vfd_acknowledged,
        .do1_valid = outputs.confirmed_valid, .tcp_connected = ctx->heartbeat.tcp_connected,
        .heartbeat_valid = ctx->status.pi_ok};
    const FaultEvent *previous = ctx->history_count ?
        &ctx->history[(ctx->history_next + FAULT_HISTORY_SIZE - 1u) % FAULT_HISTORY_SIZE] : NULL;
    if (!previous || previous->state != event.state || previous->statusword != event.statusword ||
        previous->status_valid != event.status_valid || previous->do1_requested != event.do1_requested ||
        previous->do1_acknowledged != event.do1_acknowledged || previous->do1_valid != event.do1_valid ||
        previous->tcp_connected != event.tcp_connected || previous->heartbeat_valid != event.heartbeat_valid)
    {
        ctx->history[ctx->history_next] = event;
        ctx->history_next = (ctx->history_next + 1u) % FAULT_HISTORY_SIZE;
        if (ctx->history_count < FAULT_HISTORY_SIZE)
            ctx->history_count++;
    }
    if (ctx->status.first_fault.valid && ctx->fault_episode_active)
    {
        if (!ctx->status.fault_do1_off_observed_ms && outputs.confirmed_valid &&
            !outputs.vfd_acknowledged)
            ctx->status.fault_do1_off_observed_ms = now;
        if (!ctx->status.fault_off2_observed_ms && ctx->motor.status_valid && ctx->motor.off2_active)
            ctx->status.fault_off2_observed_ms = now;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Control_CaptureFault()

static void Control_CaptureFault(ControlContext *ctx, SystemState_t previous, const char *source)
{
    FirstFaultSnapshot *f = &ctx->status.first_fault;
    if (ctx->fault_episode_active || ctx->machine.state != STATE_FAULT)
        return;
    ctx->fault_episode_active = true;
    ctx->status.fault_do1_off_observed_ms = 0;
    ctx->status.fault_off2_observed_ms = 0;
    DigitalOutputStatus outputs = {0};
    DigitalOutputs_GetStatus(&outputs);
    *f = (FirstFaultSnapshot){.valid = true, .sequence = ++ctx->fault_sequence,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .previous_state = previous, .new_state = STATE_FAULT, .code = ctx->machine.fault,
        .source = source, .heartbeat_timeout_ms = COMMAND_TIMEOUT_MS,
        .heartbeat_age_ms = ctx->heartbeat.connected ?
            (uint32_t)(xTaskGetTickCount() - ctx->heartbeat.tick) * portTICK_PERIOD_MS : UINT32_MAX,
        .heartbeat_valid = ctx->status.pi_ok, .ethernet_link = ctx->ethernet_link,
        .heartbeat = ctx->heartbeat, .configuration = ctx->status.configuration,
        .pi_last_message_ms = (uint32_t)ctx->heartbeat.last_message_tick * portTICK_PERIOD_MS,
        .motor = ctx->motor, .safety = ctx->safety,
        .do1_requested = outputs.vfd_requested, .do1_acknowledged = outputs.vfd_acknowledged,
        .do1_valid = outputs.confirmed_valid, .history_count = ctx->history_count};
    const unsigned first = (ctx->history_next + FAULT_HISTORY_SIZE - ctx->history_count) % FAULT_HISTORY_SIZE;
    for (unsigned i = 0; i < ctx->history_count; ++i)
        f->history[i] = ctx->history[(first + i) % FAULT_HISTORY_SIZE];
}

///////////////////////////////////////////////////////////////////////////////
// Control_Fault()

static void Control_Fault(ControlContext *ctx, FaultReason_t fault, const char *source)
{
    const SystemState_t previous = ctx->machine.state;
    StateMachine_Fault(&ctx->machine, fault, ctx->now_ms);
    Control_CaptureFault(ctx, previous, source);
}

///////////////////////////////////////////////////////////////////////////////
// Control_PublishStatus()

/*
 * Publishes current state and motor demand to the existing overwrite queues.
 * Called before Modbus service and again after same-cycle diagnostic fault
 * handling.
 */
static void Control_PublishStatus(ControlContext *ctx)
{
    Control_RecordEvent(ctx);
    ctx->status.control_ready = ctx->tasks_ready;
    ctx->status.control_clock_started = ctx->control_clock_started;
    ctx->status.init_deadline_exceeded =
        ctx->machine.state == STATE_INIT &&
        (uint32_t)(ctx->now_ms - ctx->machine.boot_ms) >= INIT_TIMEOUT_MS;
    ctx->status.control_cycle_count = ctx->control_cycle_count;
    ctx->status.timing_fault_execution_us = ctx->timing_stats.fault_execution_us;
    ctx->status.timing_fault_service_us = ctx->timing_stats.fault_service_us;
    ctx->status.timing_fault_missed_ticks = ctx->timing_stats.fault_missed_ticks;
    ctx->status.timing_fault_reason = ctx->timing_stats.fault_reason;
    ctx->status.modbus_initialized = ctx->v20_initialized;
    ctx->status.state = ctx->machine.state;
    ctx->status.fault_from = ctx->machine.fault_from;
    ctx->status.fault = ctx->machine.fault;
    ctx->status.last_fault = ctx->machine.last_fault;
    ctx->status.running = ctx->machine.state == STATE_RUNNING;
    ctx->status.reset_required = ctx->machine.state == STATE_FAULT;
    ctx->status.reason = ctx->machine.fault == FAULT_NONE ? StateMachine_Name(ctx->machine.state)
                                                          : FaultReason_Name(ctx->machine.fault);
    ctx->status.transitions = ctx->machine.transition_count;
    ReadyDiagnostics *diag = &ctx->status.ready_diag;
    diag->safety_inputs_valid = ctx->safety.valid;
    diag->safety_inputs_fresh =
        ctx->safety.valid &&
        ctx->now_ms - ctx->safety.tick_ms < INPUT_TIMEOUT_MS;
    diag->estop_released = !ctx->safety.estop_pressed;
    diag->stop_ok = ctx->safety.stop_ok;
    diag->reset_detected = ctx->reset;
    diag->reset_accepted = ctx->machine.reset_accepted;
    diag->reset_pending = ctx->machine.reset_pending;
    diag->safety_commissioned = ctx->safety.commissioned;
    diag->safety_relay_ok = ctx->safety.relay_ok;
    diag->relay_feedback_ok = ctx->safety.relay_ok;
    diag->safety_outputs_healthy = ctx->safety.outputs_ok;
    diag->modbus_initialized = ctx->motor.initialized;
    diag->modbus_response_received = ctx->motor.status_valid;
    diag->v20_status_healthy = ctx->motor.status_healthy;
    diag->v20_status_word = ctx->motor.status;
    diag->v20_ready = ctx->motor.ready;
    diag->v20_ready_to_switch_on = ctx->motor.ready_to_switch_on;
    diag->v20_ready_to_operate = ctx->motor.ready_to_operate;
    diag->v20_operation_enabled = ctx->motor.operation_enabled;
    diag->v20_off2_active = ctx->motor.off2_active;
    diag->v20_off3_active = ctx->motor.off3_active;
    diag->v20_switch_on_inhibited = ctx->motor.switch_on_inhibited;
    diag->v20_control_word_requested = ctx->motor.control_word_requested;
    diag->v20_control_word_acknowledged = ctx->motor.control_word_acknowledged;
    diag->v20_control_word_requested_valid = ctx->motor.control_word_requested_valid;
    diag->v20_control_word_acknowledged_valid = ctx->motor.control_word_acknowledged_valid;
    diag->drive_communication_ok = ctx->motor.communication_ok;
    diag->v20_run_commissioned = V20_RUN_COMMISSIONED;
    diag->drive_fault_free = ctx->motor.drive_fault_free;
    diag->operational_ready = ctx->motor.operational_ready;
    diag->run_authorized = ctx->motor.run_authorized;
    diag->diagnostics_complete = ctx->motor.diagnostics_complete;
    diag->reference_valid = ctx->motor.reference_valid;
    diag->reference_config_valid = ctx->motor.reference_config_valid;
    diag->watchdog_valid = ctx->motor.watchdog_valid;
    diag->watchdog_config_known = ctx->motor.watchdog_config_known;
    diag->watchdog_config_valid = ctx->motor.watchdog_config_valid;
    diag->reference_value = ctx->motor.reference_value;
    diag->watchdog_register_value = ctx->motor.watchdog_register_value;
    diag->watchdog_parameter_expected_value = ctx->motor.watchdog_parameter_expected_value;
    diag->modbus_last_error = ctx->motor.last_error;
    diag->modbus_last_operation = ctx->motor.last_operation;
    diag->modbus_last_error_register = ctx->motor.last_error_register;
    diag->modbus_last_error_expected = ctx->motor.last_error_expected;
    diag->modbus_last_error_actual = ctx->motor.last_error_actual;
    diag->modbus_exception_code = ctx->motor.exception_code;
    diag->modbus_last_success_age_ms = ctx->motor.last_success_age_ms;
    diag->modbus_errors = ctx->motor.errors;
    diag->modbus_timeouts = ctx->motor.timeouts;
    diag->pitot_communication_ok = SensorLib_PitotCommunicationOk();
    diag->sensor_data_valid = ctx->sample.valid;
    diag->sensor_data_fresh = ctx->status.sensor_ok;
    diag->sensor_ok = ctx->status.sensor_ok;
    diag->pi_ok = ctx->status.pi_ok;
    diag->blocking_reasons = StateMachine_GetBlockingReasons(&ctx->machine, &ctx->inputs);
    DigitalOutputStatus outputs = {0};
    DigitalOutputs_GetStatus(&outputs);
    diag->digital_outputs_initialized = outputs.initialized;
    diag->digital_outputs_healthy = outputs.initialized && outputs.confirmed_valid;
    diag->do1_requested = outputs.vfd_requested;
    diag->do1_acknowledged = outputs.vfd_acknowledged;
    diag->do1_ack_valid = outputs.confirmed_valid;
    if (!ctx->status.running)
        ctx->status.output = 0.0f;

    ctx->status.output =
        fminf(100.0f * MANUAL_MAX_HZ / V20_REFERENCE_HZ, fmaxf(0.0f, ctx->status.output));
    if (ctx->status.mode == MODE_AUTO)
        ctx->status.applied_hz = ctx->status.output * V20_REFERENCE_HZ / 100.0f;
    const MotorRequest command = {.output = ctx->status.output,
                                  .tick = ctx->now_tick,
                                  .run = ctx->status.running || ctx->machine.state == STATE_STARTING,
                                  .permissive = ctx->machine.permissive,
                                  .coast = ctx->machine.state == STATE_FAULT,
                                  .epoch = ctx->machine.epoch};
    xQueueOverwrite(motor_requests, &command);
    xQueueOverwrite(telemetry, &ctx->status);
}

///////////////////////////////////////////////////////////////////////////////
// Control_UpdateDiagnostics()

/*
 * Accounts for execution time and faults immediately on overruns or lost drive
 * communication.
 */
static void Control_UpdateDiagnostics(ControlContext *ctx)
{
    ctx->status.stack_free = uxTaskGetStackHighWaterMark(NULL);
    ctx->status.execution_us = (uint32_t)(esp_timer_get_time() - ctx->started_us);
    if (!ControlTiming_Execution(&ctx->timing_stats, ctx->status.execution_us,
                                 ctx->status.service_us))
    {
        ctx->latched_timing_fault = FAULT_CONTROL_OVERRUN;
        Control_Fault(ctx, ctx->latched_timing_fault, "control_timing");
        V20Modbus_Disarm();
    }
    /* State-specific drive diagnosis belongs to StateMachine_Step. Only transport
     * loss is acted upon here immediately after service; never relabel a VFD trip. */
    if (!ctx->v20_initialized ||
        (!ctx->motor.communication_ok &&
         (ctx->machine.state == STATE_STARTING || ctx->machine.state == STATE_RUNNING ||
          ctx->machine.state == STATE_READY || ctx->machine.state == STATE_STOP)))
    {
        Control_Fault(ctx, FAULT_MODBUS, "control");
        V20Modbus_Disarm();
    }
    ctx->status.overruns = ctx->timing_stats.overruns;
}

///////////////////////////////////////////////////////////////////////////////
// Control_BeginCycle()

/*
 * Records the timer wakeup, refreshes the watchdog and latches missed deadlines
 * without replaying steps.
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
    if (notifications > 0)
        ctx->control_cycle_count++;
    if (ctx->started_us - ctx->window_start_us >= 1000000)
    {
        ctx->status.control_hz =
            (float)ctx->cycles * 1000000.0f / (float)(ctx->started_us - ctx->window_start_us);
        ctx->cycles = 0;
        ctx->window_start_us = ctx->started_us;
    }
}

///////////////////////////////////////////////////////////////////////////////
// ControlTask()

/*
 * Coordinates initialization and the ordered 100 Hz control cycle on Core 1.
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
    ctx->control_clock_started = true;

    for (;;)
    {
        const uint32_t notifications =
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
        Control_BeginCycle(ctx, notifications);
        const EventBits_t buttonBits = Control_ReadButtonEvents();
        Control_ReadInputs(ctx);
        Control_RecordEvent(ctx);
        Control_ProcessCommands(ctx, buttonBits);
        Control_AcquirePitot(ctx);
        Control_CheckSafety(ctx);
        Control_UpdateStateMachine(ctx);
        Control_UpdateController(ctx);
        Control_PublishStatus(ctx);
        Control_UpdateMotor(ctx);
        Control_UpdateDiagnostics(ctx);
        Control_PublishStatus(ctx);
    }
}
