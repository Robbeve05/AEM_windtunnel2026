/*
 * ControlTask.c
 *
 * Windtunnel control loop, synchronised to the external 1 kHz clock.
 */
#include "TaskData.h"
///////////////////////////////////////////////////////////////////////////////
// system includes
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// ESP32 HAL includes
#include "driver/gpio.h"
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

///////////////////////////////////////////////////////////////////////////////
// static void ControlClockInterrupt(void *argument)
/*
 * Wordt op iedere opgaande flank van de externe 1 kHz-klok aangeroepen.
 * De ISR doet geen sensor-, regel- of motor-I/O; hij wekt alleen ControlTask.
 */
static void IRAM_ATTR ControlClockInterrupt(void *argument)
{
    (void)argument;

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (control_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(control_task_handle, &higherPriorityTaskWoken);
        if (higherPriorityTaskWoken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// static void ConfigureControlClock(void)
/*
 * Configureert de externe klok als opgaande-flankinterrupt op CONTROL_CLOCK_GPIO.
 */
static void ConfigureControlClock(void)
{
    const gpio_config_t clockPin = {
        .pin_bit_mask = 1ULL << CONTROL_CLOCK_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&clockPin));
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(gpio_isr_handler_add(
        (gpio_num_t)CONTROL_CLOCK_GPIO, ControlClockInterrupt, NULL));
    ESP_ERROR_CHECK(gpio_set_intr_type(
        (gpio_num_t)CONTROL_CLOCK_GPIO, GPIO_INTR_POSEDGE));
}

///////////////////////////////////////////////////////////////////////////////
// static void PublishControlState(Telemetry *state, TickType_t now)
/*
 * Publiceert de actuele regelstatus en de laatst berekende motoropdracht.
 */
static void PublishControlState(Telemetry *state, TickType_t now)
{
    const MotorRequest request = {.output = state->output, .tick = now};
    xQueueOverwrite(motor_requests, &request);
    xQueueOverwrite(telemetry, state);
}

///////////////////////////////////////////////////////////////////////////////
// void ControlTask(void *argument)
/*
 * Verwerkt op iedere externe klokflank de PI(D)-regelaar en systeemstatus.
 */
void ControlTask(void *argument)
{
    (void)argument;

    control_task_handle = xTaskGetCurrentTaskHandle();
    ConfigureControlClock();

    Regelaar pid;
    ESP_ERROR_CHECK(Regelaar_Init(&pid, DEFAULT_KP, DEFAULT_KI, DEFAULT_KD,
        CONTROL_CLOCK_PERIOD_S, 0.0f, MAX_OUTPUT, 0.05f) ? ESP_OK : ESP_FAIL);

    TickType_t lastCommand = xTaskGetTickCount();
    Telemetry state = {.reason = "STOPPED"};
    PublishControlState(&state, xTaskGetTickCount());

    for (;;)
    {
        /* Een gemiste klok betekent dat de vaste Tustin-sampletijd niet geldt. */
        const uint32_t clockTicks = ulTaskNotifyTake(
            pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
        const TickType_t now = xTaskGetTickCount();

        if (clockTicks == 0U)
        {
            state.running = false;
            state.output = 0.0f;
            state.reason = "CLOCK_TIMEOUT";
            Regelaar_Reset(&pid);
            PublishControlState(&state, now);
            continue;
        }

        /* Verwerk geen meerdere klokflanken met dezelfde meting als sample. */
        if (clockTicks != 1U)
        {
            state.running = false;
            state.output = 0.0f;
            state.reason = "CLOCK_OVERRUN";
            Regelaar_Reset(&pid);
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
            (TickType_t)(now - motor.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);

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
                    if (!stopRequested && state.sensor_ok && state.motor_ok)
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
                        if (Regelaar_Init(&pid, command.values[0], command.values[1],
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
                    stopRequested = true;
                    break;

                case CMD_PING:
                    break;
            }
        }

        if (stopRequested)
        {
            state.running = false;
            state.reason = "STOPPED";
        }
        else if (!state.sensor_ok)
        {
            state.running = false;
            state.reason = "SENSOR_FAULT";
        }
        else if (!state.motor_ok)
        {
            state.running = false;
            state.reason = "MOTOR_FAULT";
        }
        else if ((TickType_t)(now - lastCommand) >= pdMS_TO_TICKS(COMMAND_TIMEOUT_MS))
        {
            state.running = false;
            state.reason = "COMMAND_TIMEOUT";
        }

        state.output = 0.0f;
        if (state.running &&
            !Regelaar_Step(&pid, state.setpoint, sample.value, &state.output))
        {
            state.running = false;
            state.reason = "PID_FAULT";
        }

        if (!state.running)
        {
            state.output = 0.0f;
            Regelaar_Reset(&pid);
        }

        PublishControlState(&state, now);
    }
}
