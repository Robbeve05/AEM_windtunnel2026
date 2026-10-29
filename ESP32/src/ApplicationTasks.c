/*
 * ApplicationTasks.c
 * Creates communication infrastructure before starting any application task.
 *
 * Author: R.E. van Eekelen
 */
///////////////////////////////////////////////////////////////////////////////
// Includes

#include "ApplicationTasks.h"
#include "SafetyIO.h"
#include "I2CBus.h"
#include "TaskData.h"
#include "PiProtocol.h"
#include "freertos/task.h"
#include "esp_err.h"

///////////////////////////////////////////////////////////////////////////////
// Shared handles

QueueHandle_t commands, sensors, motor_requests, motor_status, telemetry, safety_status,
    pi_heartbeat;
/* Retained separately for existing STOP_REQUEST producers. */
EventGroupHandle_t events;
EventGroupHandle_t handle_ButtonEventGroup;
EventGroupHandle_t handle_ThreadEventGroup;
EventGroupHandle_t handle_SystemEventGroup;
TaskHandle_t control_task_handle;

///////////////////////////////////////////////////////////////////////////////
// void StartApplicationTasks(void)
/*
 * Allocates the existing queues and all EventGroups before task creation.
 * Task stacks, priorities and CPU assignments retain their existing values.
 */
void StartApplicationTasks(void)
{
    SafetyIO_Init();
    (void)I2CBus_Init(); /* Failed I/O remains inhibited through output diagnostics. */
    safety_status = xQueueCreate(1, sizeof(SafetySnapshot));
    pi_heartbeat = xQueueCreate(1, sizeof(PiHeartbeat));
    commands = xQueueCreate(16, sizeof(PiCommand));
    sensors = xQueueCreate(1, sizeof(Sample));
    motor_requests = xQueueCreate(1, sizeof(MotorRequest));
    motor_status = xQueueCreate(1, sizeof(MotorStatus));
    telemetry = xQueueCreate(1, sizeof(Telemetry));
    events = xEventGroupCreate();
    handle_ButtonEventGroup = xEventGroupCreate();
    handle_ThreadEventGroup = xEventGroupCreate();
    handle_SystemEventGroup = xEventGroupCreate();
    ESP_ERROR_CHECK(commands && sensors && motor_requests && motor_status && telemetry && events &&
                            safety_status && pi_heartbeat && handle_ButtonEventGroup &&
                            handle_ThreadEventGroup && handle_SystemEventGroup
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SafetyTask, "SafetyTask", 3072, NULL, 5, NULL, 0) ==
                            pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, NULL, 3, NULL, 0) ==
                            pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ControlTask, "ControlTask", 6144, NULL, 4,
                                            &control_task_handle, 1) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(PiCommunicationTask, "PiCommunication", 6144, NULL, 2,
                                            NULL, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
