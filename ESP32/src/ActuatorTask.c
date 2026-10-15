#include "TaskData.h"
#include "AppConfig.h"
#include "WindtunnelHAL.h"
#include "freertos/task.h"
void ActuatorTask(void *argument)
{
    (void)argument;
    bool ready = MotorLib_Init();
    MotorRequest request = {0};
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xQueuePeek(motor_requests, &request, 0);
        TickType_t now = xTaskGetTickCount();
        /* Lokale watchdog: vraag nul aan als de regeltaak geen nieuwe output levert. */
        bool fresh = (TickType_t)(now-request.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
        bool ok = ready && MotorLib_Write(fresh ? request.output : 0);
        MotorStatus status = {.ready=ready, .ok=ok, .tick=xTaskGetTickCount()};
        xQueueOverwrite(motor_status, &status);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(ACTUATOR_PERIOD_MS));
    }
}
