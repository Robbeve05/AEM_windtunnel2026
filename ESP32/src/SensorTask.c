#include "TaskData.h"
#include "AppConfig.h"
#include "PressureSensors.h"
#include "freertos/task.h"
#include <math.h>
void SensorTask(void *argument)
{
    (void)argument;
    bool ready = SensorLib_Init();
    TickType_t last = xTaskGetTickCount();
    uint32_t sequence=0;
    for (;;) {
        Sample s = {.tick=xTaskGetTickCount()};
        s.valid = ready && SensorLib_Read(&s.value) && isfinite(s.value) && s.value >= 0;
        SensorLib_Details(&s);
        s.sequence=++sequence;
        xQueueOverwrite(sensors, &s);
        if (xTaskGetTickCount()-last >= pdMS_TO_TICKS(SENSOR_PERIOD_MS)) last=xTaskGetTickCount();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}
