/* Author: R.E. van Eekelen.
 * Core 0 pressure taps receive a GPTimer notification 2.5 ms after the
 * Core 1 pitot/regulator notification. Only one I2C operation per slot.
 * Each normal wake is 5 ms apart. No I2C access from an ISR.
 */
#include "TaskData.h"
#include "AppConfig.h"
#include "PressureSensors.h"
#include "freertos/task.h"

void SensorTask(void *argument)
{
    (void)argument;
    for (;;) {
        const uint32_t notifications = ulTaskNotifyTake(
            pdTRUE, pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));
        if (notifications == 0) continue;
        /* Missed notifications are intentionally not replayed. */
        Sample taps = {0};
        if (SensorLib_TapsStep(&taps))
            xQueueOverwrite(tap_samples, &taps);
    }
}
