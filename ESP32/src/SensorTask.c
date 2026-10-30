/* Author: R.E. van Eekelen. All I2C stays on Core 0, 2.5ms after control.
 * Missed slots are never replayed. Last successful pitot time is authoritative.
 */
#include "ApplicationTasks.h"
#include "TaskData.h"
#include "AppConfig.h"
#include "PressureSensors.h"
#include "esp_log.h"
#include <string.h>
void SensorTask(void *argument) {
    (void)argument;
    bool ready = SensorLib_Init();
    Sample taps = {0};
    if (ready) xEventGroupSetBits(handle_ThreadEventGroup, EVT_SENSOR_TASK_READY);
    else ESP_LOGE("SensorTask", "Sensor initialization failed; startup inhibited");
    for (;;) {
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS)) || !ready) continue;
        Sample sample = {0};
        if (SensorLib_PitotStep(&sample)) {
            memcpy(sample.taps, taps.taps, sizeof(sample.taps));
            sample.taps_valid = taps.taps_valid;
            memcpy(sample.tap_ticks, taps.tap_ticks, sizeof(sample.tap_ticks));
            for (unsigned i=0;i<16;i++)
                if ((TickType_t)(xTaskGetTickCount()-taps.tap_ticks[i]) >= pdMS_TO_TICKS(TAP_TIMEOUT_MS))
                    sample.taps_valid &= (uint16_t)~(1u<<i);
            sample.errors += taps.errors;
            xQueueOverwrite(sensors, &sample);
            xEventGroupSetBits(handle_SystemEventGroup, EVT_SENSOR_UPDATED);
        }
        /* One incremental mux/conversion operation, with no conversion delay. */
        (void)SensorLib_TapsStep(&taps);
    }
}
