///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * main.c
 *
 * ESP32 application startup and runtime diagnostics.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "ApplicationTasks.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static void MonitorTaskStacks(void);

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// MonitorTaskStacks()

static void MonitorTaskStacks(void)
{
    static bool reported, low_reported;
    static unsigned last_reported[7];
    TaskHandle_t handles[] = {input_task_handle, sensor_task_handle, control_task_handle,
        pi_task_handle, heartbeat_task_handle, fault_diagnostics_task_handle, xTaskGetCurrentTaskHandle()};
    unsigned minimum[7];
    bool low = false, declined = false;
    for (unsigned i = 0; i < 7; ++i) {
        minimum[i] = handles[i] ? (unsigned)uxTaskGetStackHighWaterMark(handles[i]) : 0;
        low = low || (handles[i] && minimum[i] < 512u);
        declined = declined || (last_reported[i] > minimum[i] && last_reported[i] - minimum[i] >= 128u);
    }
    /* Still sample every five seconds. Quiet healthy operation; report a new
     * low condition or a further significant decline instead of repeating it. */
    if (!reported || (low && (!low_reported || declined))) {
        if (low)
            ESP_LOGW("Diag", "stack_low min_bytes input=%u sensor=%u control=%u pi=%u heartbeat=%u fault_diag=%u main=%u heap=%lu",
                     minimum[0], minimum[1], minimum[2], minimum[3], minimum[4], minimum[5], minimum[6],
                     (unsigned long)esp_get_free_heap_size());
        else
            ESP_LOGI("Diag", "stack_min_bytes input=%u sensor=%u control=%u pi=%u heartbeat=%u fault_diag=%u main=%u heap=%lu",
                     minimum[0], minimum[1], minimum[2], minimum[3], minimum[4], minimum[5], minimum[6],
                     (unsigned long)esp_get_free_heap_size());
        for (unsigned i = 0; i < 7; ++i) last_reported[i] = minimum[i];
        reported = true;
        low_reported = low;
    }
}

///////////////////////////////////////////////////////////////////////////////
// app_main()

void app_main(void)
{
    ESP_LOGI("Startup", "reset=%d heap=%lu", (int)esp_reset_reason(),
             (unsigned long)esp_get_free_heap_size());
    StartApplicationTasks();
    ESP_ERROR_CHECK(heap_caps_check_integrity_all(true) ? ESP_OK : ESP_FAIL);
    ESP_LOGI("Startup", "Application tasks created; physical readiness remains interlocked");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_ERROR_CHECK(heap_caps_check_integrity_all(true) ? ESP_OK : ESP_FAIL);
        MonitorTaskStacks();
    }
}
