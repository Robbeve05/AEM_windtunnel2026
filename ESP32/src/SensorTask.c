#include "TaskData.h"
#include "AppConfig.h"
#include "WindtunnelHAL.h"
#include "freertos/task.h"
#include <math.h>
void SensorTask(void *argument)
{
    (void)argument;
    bool ready = SensorLib_Init();
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        Sample s = {0};
        s.valid = ready && SensorLib_Read(&s.value) && isfinite(s.value) && s.value >= 0;
        s.tick = xTaskGetTickCount();
        xQueueOverwrite(sensors, &s);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}
