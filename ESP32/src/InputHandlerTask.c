/*
 * InputHandlerTask.c
 * Physical input validation and operational K2 output management.
 *
 * Author: R.E. van Eekelen
 */
#include "ApplicationTasks.h"
#include "AppConfig.h"
#include "TaskData.h"
#include "SafetyIO.h"
#include "I2CBus.h"
#include "esp_task_wdt.h"
#include "esp_err.h"
#include "esp_log.h"
///////////////////////////////////////////////////////////////////////////////
// File-local button state

typedef struct
{
    bool initialized, start_raw, start_stable, reset_raw, reset_stable, start_armed, reset_armed,
        reset_candidate;
    uint32_t start_changed, reset_changed;
} Buttons;

///////////////////////////////////////////////////////////////////////////////
// static void Buttons_Update(Buttons *b, SafetySnapshot *s, uint32_t now)
/*
 * Debounces START on press and RESET on release. Preserves the existing
 * arming, validity and emergency inhibition conditions and event timestamps.
 */
static void Buttons_Update(Buttons *b, SafetySnapshot *s, uint32_t now)
{
    if (!b->initialized)
    {
        *b = (Buttons){.initialized = true,
                       .start_raw = s->start_pressed,
                       .start_stable = s->start_pressed,
                       .reset_raw = s->reset_pressed,
                       .reset_stable = s->reset_pressed,
                       .start_changed = now,
                       .reset_changed = now};
        return;
    }
    if (!s->valid || s->estop_pressed)
    {
        b->reset_armed = false;
        b->reset_candidate = false;
        b->start_armed = false;
    }
    if (s->start_pressed != b->start_raw)
    {
        b->start_raw = s->start_pressed;
        b->start_changed = now;
    }
    if (s->reset_pressed != b->reset_raw)
    {
        b->reset_raw = s->reset_pressed;
        b->reset_changed = now;
    }
    if (now - b->start_changed >= BUTTON_DEBOUNCE_MS)
    {
        if (!b->start_raw)
            b->start_armed = true;
        if (b->start_raw && !b->start_stable && b->start_armed && s->valid && !s->estop_pressed &&
            !s->reset_pressed)
        {
            s->start_sequence++;
            s->start_ms = now;
            b->start_armed = false;
        }
        b->start_stable = b->start_raw;
    }
    if (now - b->reset_changed >= BUTTON_DEBOUNCE_MS)
    {
        if (b->reset_raw && !b->reset_stable && b->reset_armed && s->valid && !s->estop_pressed)
        {
            b->reset_candidate = true;
            b->reset_armed = false;
        }
        if (!b->reset_raw && b->reset_stable && b->reset_candidate && !s->estop_pressed && s->valid)
        {
            s->reset_sequence++;
            s->reset_ms = now;
            b->reset_candidate = false;
        }
        if (!b->reset_raw && !s->estop_pressed && s->valid)
            b->reset_armed = true;
        b->reset_stable = b->reset_raw;
    }
}

///////////////////////////////////////////////////////////////////////////////
// void InputHandlerTask(void *argument)
/*
 * Owns button validation and K2 output updates. Publishes notifications only
 * after the matching sequence and timestamp are visible in safety_status.
 */
void InputHandlerTask(void *argument)
{
    (void)argument;
    Buttons buttons = {0};
    SafetySnapshot previous = {0};
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    TickType_t wake = xTaskGetTickCount();
    bool initialized = false;
    for (;;)
    {
        SafetySnapshot s = SafetyIO_Read();
        s.start_sequence = previous.start_sequence;
        s.reset_sequence = previous.reset_sequence;
        s.start_ms = previous.start_ms;
        s.reset_ms = previous.reset_ms;
        Buttons_Update(&buttons, &s, s.tick_ms);
        MotorRequest request = {0};
        xQueuePeek(motor_requests, &request, 0);
        bool fresh = xTaskGetTickCount() - request.tick < pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS);
        bool enable = fresh && request.permissive && !SafetyIO_Inhibited();
        s.outputs_ok = DigitalOutputs_Set(enable);
        xQueueOverwrite(safety_status, &s);
        SafetyIO_PublishButtonEvents(&s, &previous);
        previous = s;
        if (!initialized)
        {
            /* Only a successful first output initialization pass publishes readiness. */
            initialized = true;
            if (s.outputs_ok)
                xEventGroupSetBits(handle_ThreadEventGroup, EVT_INPUT_TASK_READY);
            else
                ESP_LOGE("InputHandler", "Output initialization failed; startup remains inhibited");
        }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}
