///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * SensorTask.c
 *
 * Pressure taps only, on Core 0, in the retained 5ms timer phase.
 * Pitot acquisition and state belong exclusively to ControlTask on Core 1.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_log.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "ApplicationTasks.h"
#include "PressureSensors.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// SensorTask()

void SensorTask(void *argument)
{
    (void)argument;
    PressureTapSample taps = {0};
    /* No Pitot initialization/state is touched on this core. */
    xEventGroupSetBits(handle_ThreadEventGroup, EVT_SENSOR_TASK_READY);
    ESP_LOGI("SensorTask", "Pressure tap acquisition initialized");
    for (;;)
    {
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS))) continue;
        if (SensorLib_TapsStep(&taps))
        {
            xQueueOverwrite(pressure_taps, &taps);
            xEventGroupSetBits(handle_SystemEventGroup, EVT_SENSOR_UPDATED);
        }
    }
}
