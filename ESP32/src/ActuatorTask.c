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
        bool fresh = (TickType_t)(now-request.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
        /* Explicit OFF1 on stopped/stale requests, even when PI output was already zero. */
        fresh = fresh && !(xEventGroupGetBits(events)&INTERLOCK_OPEN);
        bool ok = ready && MotorLib_Write(fresh && request.run, fresh ? request.output : 0);
        MotorStatus status = {.ready=ready, .ok=ok, .tick=xTaskGetTickCount()};
        MotorLib_Details(&status);
        xQueueOverwrite(motor_status, &status);
        /* No burst of catch-up requests after a Modbus timeout. */
        if ((TickType_t)(xTaskGetTickCount()-last) >= pdMS_TO_TICKS(ACTUATOR_PERIOD_MS))
            last=xTaskGetTickCount();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(ACTUATOR_PERIOD_MS));
    }
}
