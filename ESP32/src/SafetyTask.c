/* Author: R.E. van Eekelen. Polling backstop and non-safety K2 output owner. */
#include "ApplicationTasks.h"
#include "AppConfig.h"
#include "TaskData.h"
#include "SafetyIO.h"
#include "Buttons.h"
#include "I2CBus.h"
#include "esp_task_wdt.h"
#include "esp_err.h"
///////////////////////////////////////////////////////////////////////////////
// void SafetyTask(void *argument)
/*
 * Owns button validation and K2 output updates. Publishes notifications only
 * after the matching sequence and timestamp are visible in safety_status.
 */
void SafetyTask(void *argument)
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
            /* First input/output pass completed; health is still checked at runtime. */
            initialized = true;
            xEventGroupSetBits(handle_ThreadEventGroup, EVT_SAFETY_TASK_READY);
        }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}
