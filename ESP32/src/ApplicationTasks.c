/*
 * ApplicationTasks.c
 * Creates communication infrastructure before starting any application task.
 *
 * Author: R.E. van Eekelen
 */
///////////////////////////////////////////////////////////////////////////////
// Includes

#include "ApplicationTasks.h"
#include "OtaUpdate.h"
void HeartbeatTask(void *argument);
#include "SafetyIO.h"
#include "I2CBus.h"
#include "TaskData.h"
#include "PiProtocol.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

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
TaskHandle_t sensor_task_handle;

///////////////////////////////////////////////////////////////////////////////
// static void InitHardware(void)
/*
 * Configures physical inputs and loads inactive output latches before tasks start.
 */
static void InitHardware(void)
{
    SafetyIO_Init();
    if (!I2CBus_Init())
        ESP_LOGE("ApplicationTasks", "I2C/output initialization failed; outputs remain inhibited");
}

///////////////////////////////////////////////////////////////////////////////
// static void CreateQueues(void)
/*
 * Creates the existing command and one-element latest-value queues.
 */
static void CreateQueues(void)
{
    safety_status = xQueueCreate(1, sizeof(SafetySnapshot));
    pi_heartbeat = xQueueCreate(1, sizeof(PiHeartbeat));
    commands = xQueueCreate(16, sizeof(PiCommand));
    sensors = xQueueCreate(1, sizeof(Sample));
    motor_requests = xQueueCreate(1, sizeof(MotorRequest));
    motor_status = xQueueCreate(1, sizeof(MotorStatus));
    telemetry = xQueueCreate(1, sizeof(Telemetry));
    ESP_ERROR_CHECK(commands && sensors && motor_requests && motor_status && telemetry &&
                            safety_status && pi_heartbeat
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

///////////////////////////////////////////////////////////////////////////////
// static void CreateEventGroups(void)
/*
 * Creates all event interfaces before any producer or consumer can run.
 */
static void CreateEventGroups(void)
{
    events = xEventGroupCreate();
    handle_ButtonEventGroup = xEventGroupCreate();
    handle_ThreadEventGroup = xEventGroupCreate();
    handle_SystemEventGroup = xEventGroupCreate();
    ESP_ERROR_CHECK(events && handle_ButtonEventGroup && handle_ThreadEventGroup &&
                            handle_SystemEventGroup
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

///////////////////////////////////////////////////////////////////////////////
// static void CreateTasks(void)
/*
 * Preserves task stacks, priorities, CPU assignments and the startup barrier.
 */
static void CreateTasks(void)
{
    ESP_ERROR_CHECK(
        xTaskCreatePinnedToCore(InputHandlerTask, "InputHandler", 3072, NULL, 5, NULL, 0) == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, NULL, 3,
                                            &sensor_task_handle, 0) == pdPASS
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
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(HeartbeatTask, "Heartbeat", 3072, NULL, 1,
                                            NULL, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(OtaUpdateTask, "OTA", 8192, NULL, 1,
                                            NULL, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(OtaMarkValidTask, "OTAValid", 3072, NULL, 1,
                                            NULL, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

///////////////////////////////////////////////////////////////////////////////
// void StartApplicationTasks(void)
/*
 * Initializes hardware and shared communication before creating application tasks.
 */
void StartApplicationTasks(void)
{
    InitHardware();
    CreateQueues();
    CreateEventGroups();
    CreateTasks();
}
