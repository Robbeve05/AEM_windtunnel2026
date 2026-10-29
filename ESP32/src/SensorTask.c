/*
 * SensorTask.c
 * Samples pressure data and publishes the latest measurements.
 *
 * Author: R.E. van Eekelen
 */
///////////////////////////////////////////////////////////////////////////////
// Includes

#include "ApplicationTasks.h"
#include "TaskData.h"
#include "AppConfig.h"
#include "PressureSensors.h"
#include "freertos/task.h"
#include <math.h>

///////////////////////////////////////////////////////////////////////////////
// void SensorTask(void *argument)
/*
 * Announces initialization independently of sensor health or calibration.
 * Measurements and validity remain available through the one-element queue.
 */
void SensorTask(void *argument)
{
    (void)argument;
    bool ready = SensorLib_Init();
    TickType_t last = xTaskGetTickCount();
    uint32_t sequence = 0;
    xEventGroupSetBits(handle_ThreadEventGroup, EVT_SENSOR_TASK_READY);
    for (;;)
    {
        Sample s = {.tick = xTaskGetTickCount()};
        s.valid = ready && SensorLib_Read(&s.value) && isfinite(s.value) && s.value >= 0;
        SensorLib_Details(&s);
        s.sequence = ++sequence;
        xQueueOverwrite(sensors, &s);
        xEventGroupSetBits(handle_SystemEventGroup, EVT_SENSOR_UPDATED);
        if (xTaskGetTickCount() - last >= pdMS_TO_TICKS(SENSOR_PERIOD_MS))
            last = xTaskGetTickCount();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}
