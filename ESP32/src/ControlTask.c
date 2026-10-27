/*
 * ControlTask.c
 *
 * Windtunnel control loop, synchronised to the internal 1 kHz GPTimer.
 */
#include "TaskData.h"
///////////////////////////////////////////////////////////////////////////////
// system includes
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// ESP32 HAL includes
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// application includes
#include "AppConfig.h"
#include "PiProtocol.h"
#include "Regelaar.h"
#include "SetpointCurve.h"

///////////////////////////////////////////////////////////////////////////////
// static void ControlClockInterrupt(void *argument)
/*
 * Wordt op iedere alarm van de interne 1 kHz-GPTimer aangeroepen.
 * De ISR doet geen sensor-, regel- of motor-I/O; hij wekt alleen ControlTask.
 */
static bool IRAM_ATTR ControlClockInterrupt(gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *event, void *argument)
{
    (void)timer; (void)event; (void)argument;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(control_task_handle, &wake);
    /* GPTimer driver performs the ISR yield when this callback returns true. */
    return wake == pdTRUE;
}
static void ConfigureControlClock(void)
{
    gptimer_handle_t timer;
    const gptimer_config_t config = {.clk_src=GPTIMER_CLK_SRC_DEFAULT,
        .direction=GPTIMER_COUNT_UP, .resolution_hz=1000000};
    ESP_ERROR_CHECK(gptimer_new_timer(&config, &timer));
    const gptimer_alarm_config_t alarm = {.alarm_count=1000000 / CONTROL_FREQUENCY_HZ,
        .reload_count=0, .flags.auto_reload_on_alarm=true};
    const gptimer_event_callbacks_t callbacks = {.on_alarm=ControlClockInterrupt};
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, NULL));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));
}

///////////////////////////////////////////////////////////////////////////////
// static void PublishControlState(Telemetry *state, TickType_t now)
/*
 * Publiceert de actuele regelstatus en de laatst berekende motoropdracht.
 */
static void PublishControlState(Telemetry *state, TickType_t now)
{
    const MotorRequest request = {.output = state->output, .tick = now, .run = state->running};
    xQueueOverwrite(motor_requests, &request);
    xQueueOverwrite(telemetry, state);
}

///////////////////////////////////////////////////////////////////////////////
// void ControlTask(void *argument)
/*
 * Verwerkt op ieder intern timeralarm de PI(D)-regelaar en systeemstatus.
 */
void ControlTask(void *argument)
{
    (void)argument;

    control_task_handle = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    Regelaar pid;
    ESP_ERROR_CHECK(Regelaar_INIT(&pid, CONTROL_CLOCK_PERIOD_S) ? ESP_OK : ESP_FAIL);

    SetpointCurve curve = {0};
    bool curveRunning = false;

    TickType_t lastCommand = xTaskGetTickCount();
    Telemetry state = {.reason = "STOPPED"};
    PublishControlState(&state, xTaskGetTickCount());

    int64_t previous = 0, window = esp_timer_get_time();
    uint32_t cycles = 0;
    bool timingFault = false;
    ConfigureControlClock();
    for (;;)
    {
        /* Een gemiste klok betekent dat de vaste Tustin-sampletijd niet geldt. */
        const uint32_t clockTicks = ulTaskNotifyTake(
            pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
        const TickType_t now = xTaskGetTickCount();
        int64_t started = esp_timer_get_time();
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        if (previous) {
            int64_t jitter = started - previous - 1000;
            if (jitter < 0) jitter = -jitter;
            if (started-previous >= 2000 && clockTicks==1) {
                timingFault=true; state.missed_deadlines+=(started-previous)/1000-1;
            }
            if ((uint64_t)jitter > state.max_jitter_us) state.max_jitter_us = jitter;
        }
        previous = started;
        cycles++;
        if (started-window >= 1000000) {
            state.control_hz = cycles * 1000000.0f / (started-window);
            cycles=0; window=started;
        }
        state.stack_free = uxTaskGetStackHighWaterMark(NULL);

        if (clockTicks == 0U)
        {
            state.running = false;
            curveRunning = false;
            state.output = 0.0f;
            timingFault = true; state.missed_deadlines += CONTROL_CLOCK_TIMEOUT_MS;
            state.reason = "CLOCK_TIMEOUT";
            removeRegelaarHistory(&pid);
            PublishControlState(&state, now);
            continue;
        }

        /* Verwerk geen meerdere klokflanken met dezelfde meting als sample. */
        if (clockTicks != 1U)
        {
            state.running = false;
            curveRunning = false;
            state.output = 0.0f;
            timingFault = true; state.missed_deadlines += clockTicks-1;
            state.reason = "CLOCK_OVERRUN";
            removeRegelaarHistory(&pid);
            PublishControlState(&state, now);
            continue;
        }

        Sample sample = {0};
        MotorStatus motor = {0};
        xQueuePeek(sensors, &sample, 0);
        xQueuePeek(motor_status, &motor, 0);

        state.measurement = sample.value;
        state.sensor_ok = sample.valid &&
            (TickType_t)(now - sample.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
        state.motor_ok = motor.ready && motor.ok &&
            (TickType_t)(now - motor.tick) < pdMS_TO_TICKS(MOTOR_STATUS_TIMEOUT_MS);

        bool interlockOpen = (xEventGroupGetBits(events)&INTERLOCK_OPEN)!=0;
        bool stopRequested =
            (xEventGroupWaitBits(events, STOP_REQUEST, pdTRUE, pdFALSE, 0) &
                STOP_REQUEST) != 0;

        PiCommand command;
        /* Begrensde verwerking: een volle command queue blokkeert de regellus niet. */
        for (int i = 0; i < 16 && xQueueReceive(commands, &command, 0) == pdTRUE; ++i)
        {
            lastCommand = now;
            switch (command.type)
            {
                case CMD_START:
                    if (!stopRequested && !interlockOpen && !timingFault && state.sensor_ok && state.motor_ok)
                    {
                        state.running = true;
                        state.reason = "RUNNING";
                    }
                    else
                    {
                        state.reason = "NOT_READY";
                    }
                    break;

                case CMD_SETPOINT:
                    state.setpoint = command.values[0];
                    break;

                case CMD_PID:
                    if (!state.running)
                    {
                        if (Regelaar_Instellen(&pid, command.values[0], command.values[1],
                            command.values[2], CONTROL_CLOCK_PERIOD_S, 0.0f,
                            MAX_OUTPUT, 0.05f))
                        {
                            state.reason = "PID_UPDATED";
                        }
                        else
                        {
                            state.reason = "PID_FAULT";
                        }
                    }
                    else
                    {
                        state.reason = "PID_REQUIRES_STOP";
                    }
                    break;

                case CMD_STOP:
                    stopRequested = true; timingFault = false;
                    break;

                case CMD_FILTER: /* handled in the network task */
                case CMD_PING:
                    break;
            }
        }

        if (stopRequested)
        {
            state.running = false;
            curveRunning = false;
            state.reason = "STOPPED";
            if (xEventGroupWaitBits(events,RESET_TIMING_FAULT,pdTRUE,pdFALSE,0)&RESET_TIMING_FAULT)
                timingFault = false;
        }
        else if (interlockOpen) { state.running=false; state.reason="INTERLOCK_OPEN"; }
        else if (timingFault) { state.running=false; state.reason="CLOCK_FAULT"; }
        else if (!state.sensor_ok)
        {
            state.running = false;
            curveRunning = false;
            state.reason = "SENSOR_FAULT";
        }
        else if (!state.motor_ok)
        {
            state.running = false;
            curveRunning = false;
            state.reason = "MOTOR_FAULT";
        }
        else if ((TickType_t)(now - lastCommand) >= pdMS_TO_TICKS(COMMAND_TIMEOUT_MS))
        {
            state.running = false;
            curveRunning = false;
            state.reason = "COMMAND_TIMEOUT";
        }

        state.output = 0.0f;
        if (state.running)
        {
            if (!curveRunning)
                curveRunning = SetpointCurve_Init(&curve, sample.value,
                    SETPOINT_TRANSITION_TIME_S, SETPOINT_MAX_ACCELERATION_MPS2,
                    SETPOINT_MAX_JERK_MPS3);
            if (!curveRunning || !SetpointCurve_Step(&curve, state.setpoint,
                    CONTROL_CLOCK_PERIOD_S, &state.control_setpoint))
            {
                state.running = false;
                state.reason = "RAMP_FAULT";
            }
            else
            {
                // Zoals bij de deltarobot: eerst de fout, daarna de regelaar.
                const float error = state.control_setpoint - sample.value;
                if (!PIDregelaar(&pid, error, sample.value, &state.output))
                {
                    state.running = false;
                    state.reason = "PID_FAULT";
                }
            }
        }

        if (!state.running)
        {
            curveRunning = false;
            state.control_setpoint = sample.value;
            state.output = 0.0f;
            removeRegelaarHistory(&pid);
        }

        PublishControlState(&state, now);
        state.execution_us = (uint32_t)(esp_timer_get_time()-started);
        if (state.execution_us >= 1000) {
            state.overruns++; timingFault=true; state.running=false; state.output=0;
            state.reason="CONTROL_OVERRUN"; removeRegelaarHistory(&pid);
            PublishControlState(&state, now);
        }
    }
}
