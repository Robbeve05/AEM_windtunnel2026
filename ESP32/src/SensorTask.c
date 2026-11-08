///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * SensorTask.c
 *
 * All I2C stays on Core 0, 5ms before control.
 * Missed slots are never replayed. Last successful pitot time is authoritative.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_log.h"
#include <string.h>

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
    bool ready = SensorLib_Init();
    Sample taps = {0};
    if (ready)
    {
        ESP_LOGI("SensorTask", "Sensor subsystem initialized; waiting for control-clock samples");
        xEventGroupSetBits(handle_ThreadEventGroup, EVT_SENSOR_TASK_READY);
    }
    else
    {
        ESP_LOGE("SensorTask", "Sensor initialization failed; startup inhibited");
    }
    for (;;)
    {
        if (!ready)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            ready = SensorLib_Init();
            if (ready)
                xEventGroupSetBits(handle_ThreadEventGroup, EVT_SENSOR_TASK_READY);
        }
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS)) || !ready)
            continue;
        Sample sample = {0};
        if (SensorLib_PitotStep(&sample))
        {
            memcpy(sample.taps, taps.taps, sizeof(sample.taps));
            sample.taps_valid = taps.taps_valid;
            memcpy(sample.tap_ticks, taps.tap_ticks, sizeof(sample.tap_ticks));
            for (unsigned i = 0; i < 16; i++)
                if ((TickType_t)(xTaskGetTickCount() - taps.tap_ticks[i]) >=
                    pdMS_TO_TICKS(TAP_TIMEOUT_MS))
                    sample.taps_valid &= (uint16_t)~(1u << i);
            sample.errors += taps.errors;
            xQueueOverwrite(sensors, &sample);
            xEventGroupSetBits(handle_SystemEventGroup, EVT_SENSOR_UPDATED);
        }
        /* One incremental mux/conversion operation, with no conversion delay. */
        (void)SensorLib_TapsStep(&taps);
    }
}
