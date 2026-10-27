/* Author: R.E. van Eekelen.
 * Core 0 owns ONLY optional airfoil pressure taps.
 * The pitot is sampled by ControlTask on Core 1.
 */
#include "TaskData.h"
#include "AppConfig.h"
#include "PressureSensors.h"
#include "freertos/task.h"
void SensorTask(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        Sample taps = {0};
        SensorLib_ReadTaps(&taps);
        xQueueOverwrite(tap_samples, &taps);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}
